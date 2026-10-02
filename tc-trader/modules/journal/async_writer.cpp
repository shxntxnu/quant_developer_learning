#include "async_writer.hpp"
#include <iostream>

namespace tc {

AsyncWriter::AsyncWriter(std::unique_ptr<RotatingFileSink> sink)
    : sink_(std::move(sink)) {
}

AsyncWriter::~AsyncWriter() {
    stop();
}

bool AsyncWriter::start() {
    if (is_running_.load(std::memory_order_relaxed)) {
        return true;
    }

    if (!sink_) {
        return false;
    }

    if (!sink_->open()) {
        return false;
    }

    stop_requested_.store(false, std::memory_order_release);
    is_running_.store(true, std::memory_order_release);

    worker_thread_ = std::thread(&AsyncWriter::worker_loop, this);
    return true;
}

void AsyncWriter::stop() {
    if (!is_running_.load(std::memory_order_relaxed)) {
        return;
    }

    stop_requested_.store(true, std::memory_order_release);

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (sink_) {
        sink_->flush();
        sink_->close();
    }

    is_running_.store(false, std::memory_order_release);
}

bool AsyncWriter::enqueue(const TcJournalEvent& event) {
    if (!is_running_.load(std::memory_order_relaxed)) {
        return false;
    }

    if (queue_.push(event)) {
        events_enqueued_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Queue saturated (backpressure indicator for producer thread)
    queue_full_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void AsyncWriter::flush() {
    const uint64_t target = events_enqueued_.load(std::memory_order_acquire);
    while (events_written_.load(std::memory_order_acquire) < target) {
        std::this_thread::yield();
    }

    if (sink_) {
        sink_->flush();
    }
}

bool AsyncWriter::snapshot(const char* snapshot_name, const void* data, size_t len) {
    if (!sink_) return false;
    return sink_->write_snapshot(snapshot_name, data, len);
}

void AsyncWriter::set_sink(TcJournalSinkType sink_type, const char* destination_path) {
    if (sink_) {
        sink_->set_sink(sink_type, destination_path ? destination_path : "logs");
    }
}

void AsyncWriter::worker_loop() {
    TcJournalEvent batch[BatchDrainSize];

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        size_t count = 0;
        while (count < BatchDrainSize && queue_.pop(batch[count])) {
            count++;
        }

        if (count > 0) {
            sink_->write_batch(batch, count);
            events_written_.fetch_add(count, std::memory_order_release);
        } else {
            // Buffer empty: flush pending data and brief sleep to prevent 100% CPU burn
            sink_->flush();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    // Drain any remaining events in the queue before terminating
    for (;;) {
        size_t count = 0;
        while (count < BatchDrainSize && queue_.pop(batch[count])) {
            count++;
        }

        if (count > 0) {
            sink_->write_batch(batch, count);
            events_written_.fetch_add(count, std::memory_order_release);
        } else {
            break;
        }
    }

    sink_->flush();
}

} // namespace tc
