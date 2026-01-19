#ifndef DEVICE_CORE_SPSC_QUEUE_HPP
#define DEVICE_CORE_SPSC_QUEUE_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>

namespace core {

// Lock-free Single-Producer Single-Consumer queue
// Safe for use between two RP2040 cores without locks
// Producer (Core 1) calls push(), Consumer (Core 0) calls pop()
template<typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    SpscQueue() : head_(0), tail_(0) {}

    // Producer: Add item to queue (Core 1)
    // Returns true if successful, false if queue is full
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & kMask;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            // Queue is full
            return false;
        }

        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer: Remove item from queue (Core 0)
    // Returns true if successful, false if queue is empty
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire)) {
            // Queue is empty
            return false;
        }

        item = buffer_[current_head];
        head_.store((current_head + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Check if queue is empty (approximate, for consumer)
    bool empty() const {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    // Check if queue is full (approximate, for producer)
    bool full() const {
        const size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) & kMask;
        return next_tail == head_.load(std::memory_order_relaxed);
    }

    // Get approximate number of items in queue
    size_t size() const {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (tail - head) & kMask;
    }

    // Clear queue (call from consumer side only)
    void clear() {
        head_.store(tail_.load(std::memory_order_relaxed), std::memory_order_release);
    }

    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t kMask = Capacity - 1;

    T buffer_[Capacity];
    std::atomic<size_t> head_;  // Consumer reads/writes
    std::atomic<size_t> tail_;  // Producer reads/writes
};

} // namespace core

#endif // DEVICE_CORE_SPSC_QUEUE_HPP
