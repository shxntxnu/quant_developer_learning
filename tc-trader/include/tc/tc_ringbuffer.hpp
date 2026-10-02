#ifndef TC_RINGBUFFER_HPP
#define TC_RINGBUFFER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace tc {

/**
 * Single-Producer Single-Consumer (SPSC) Lock-Free Ring Buffer.
 *
 * Characteristics:
 * - Pre-allocated fixed capacity (must be a power of two).
 * - Zero dynamic heap allocation during push/pop.
 * - Cache-line aligned indices (alignas(64)) to prevent CPU false sharing.
 * - Pure acquire/release memory semantics for optimal hardware latency.
 */
template <typename T, size_t Capacity>
class TcRingBufferSPSC {
    static_assert((Capacity > 0) && ((Capacity & (Capacity - 1)) == 0),
                  "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "T must be trivially copyable for low-latency POD transfer");

public:
    TcRingBufferSPSC() : head_(0), tail_(0) {}
    ~TcRingBufferSPSC() = default;

    TcRingBufferSPSC(const TcRingBufferSPSC&) = delete;
    TcRingBufferSPSC& operator=(const TcRingBufferSPSC&) = delete;

    /**
     * Push an item into the buffer (Producer thread only).
     * Returns true on success, false if the queue is full.
     */
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if ((current_tail - current_head) >= Capacity) {
            return false; // Queue is full
        }

        buffer_[current_tail & BufferMask] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    /**
     * Pop an item from the buffer (Consumer thread only).
     * Returns true on success, false if the queue is empty.
     */
    bool pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false; // Queue is empty
        }

        item = buffer_[current_head & BufferMask];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (tail >= head) ? (tail - head) : (Capacity - (head - tail));
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr size_t BufferMask = Capacity - 1;

    // Head index updated by consumer, read by producer
    alignas(64) std::atomic<size_t> head_;

    // Tail index updated by producer, read by consumer
    alignas(64) std::atomic<size_t> tail_;

    // Cache-line aligned storage array
    alignas(64) T buffer_[Capacity];
};

/**
 * Multi-Producer Single-Consumer (MPSC) Lock-Free Bounded Queue.
 * Designed for logging / journal ingestion where multiple threads push audit events
 * and a single dedicated Thread T4 writes them asynchronously to storage.
 */
template <typename T, size_t Capacity>
class TcQueueMPSC {
    static_assert((Capacity > 0) && ((Capacity & (Capacity - 1)) == 0),
                  "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "T must be trivially copyable");

    struct Node {
        std::atomic<size_t> sequence;
        T data;
    };

public:
    TcQueueMPSC() : head_(0), tail_(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~TcQueueMPSC() = default;

    TcQueueMPSC(const TcQueueMPSC&) = delete;
    TcQueueMPSC& operator=(const TcQueueMPSC&) = delete;

    /**
     * Push an item into the queue (Safe from any producer thread).
     * Returns true on success, false if the queue is full.
     */
    bool push(const T& item) noexcept {
        Node* node;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            node = &buffer_[pos & BufferMask];
            const size_t seq = node->sequence.load(std::memory_order_acquire);
            const intptr_t dif = (intptr_t)seq - (intptr_t)pos;

            if (dif == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false; // Queue is full
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        node->data = item;
        node->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * Pop an item from the queue (Single consumer thread only).
     * Returns true on success, false if the queue is empty.
     */
    bool pop(T& item) noexcept {
        Node* node;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            node = &buffer_[pos & BufferMask];
            const size_t seq = node->sequence.load(std::memory_order_acquire);
            const intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);

            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false; // Queue is empty
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        item = node->data;
        node->sequence.store(pos + BufferMask + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr size_t BufferMask = Capacity - 1;

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) Node buffer_[Capacity];
};

} // namespace tc

#endif /* TC_RINGBUFFER_HPP */
