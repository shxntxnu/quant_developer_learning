#ifndef TC_ASYNC_WRITER_HPP
#define TC_ASYNC_WRITER_HPP

/**
 * @file async_writer.hpp
 * @brief Thread T4 asynchronous log and audit writer engine.
 *
 * Receives journal events from multiple producer threads (T0, T1, T2, T3, T5)
 * via a bounded lock-free MPSC queue and batches them to the rotating file sink.
 */

#include <atomic>
#include <thread>
#include <memory>
#include <chrono>

#include "tc/journal/tc_journal.h"
#include "tc/tc_ringbuffer.hpp"
#include "rotating_file_sink.hpp"

namespace tc {

class AsyncWriter {
public:
    // Capacity of the lock-free MPSC queue (must be power of 2, 32,768 events)
    static constexpr size_t QueueCapacity = 32768;

    // Maximum number of items drained in a single batch to amortize write syscalls
    static constexpr size_t BatchDrainSize = 512;

    /**
     * @brief Construct an AsyncWriter with a configured file sink.
     * @param sink Unique pointer to the RotatingFileSink.
     */
    explicit AsyncWriter(std::unique_ptr<RotatingFileSink> sink);

    ~AsyncWriter();

    // Non-copyable, non-movable
    AsyncWriter(const AsyncWriter&) = delete;
    AsyncWriter& operator=(const AsyncWriter&) = delete;

    /**
     * @brief Start the background worker thread (Thread T4).
     */
    bool start();

    /**
     * @brief Request graceful shutdown, drain all remaining events in the queue,
     * flush the sink to disk, and join Thread T4.
     */
    void stop();

    /**
     * @brief Enqueue an audit event for asynchronous disk persistence.
     * Lock-free and non-blocking. Safe to call from any thread concurrently.
     *
     * @param event The event record to copy into the queue.
     * @return true on success, false if the queue is saturated.
     */
    bool enqueue(const TcJournalEvent& event);

    /**
     * @brief Block until all currently enqueued events have been flushed to disk.
     */
    void flush();

    /**
     * @brief Write an atomic binary state snapshot blob to disk.
     */
    bool snapshot(const char* snapshot_name, const void* data, size_t len);

    /**
     * @brief Update the active output format and destination directory.
     */
    void set_sink(TcJournalSinkType sink_type, const char* destination_path);

    // Diagnostics & Metrics
    [[nodiscard]] uint64_t events_enqueued() const noexcept { return events_enqueued_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t events_written() const noexcept { return events_written_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t queue_full_count() const noexcept { return queue_full_count_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(std::memory_order_relaxed); }

private:
    /**
     * @brief Worker thread loop executed on Thread T4.
     */
    void worker_loop();

    std::unique_ptr<RotatingFileSink> sink_;
    TcQueueMPSC<TcJournalEvent, QueueCapacity> queue_;

    std::thread worker_thread_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> stop_requested_{false};

    // Performance metrics
    alignas(64) std::atomic<uint64_t> events_enqueued_{0};
    alignas(64) std::atomic<uint64_t> events_written_{0};
    alignas(64) std::atomic<uint64_t> queue_full_count_{0};
};

} // namespace tc

#endif /* TC_ASYNC_WRITER_HPP */
