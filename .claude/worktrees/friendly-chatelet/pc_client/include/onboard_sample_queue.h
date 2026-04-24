#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <cstdint>

namespace powermonitor {
namespace client {

// Forward declaration
struct OnboardSample;

/**
 * @brief Thread-safe queue for onboard power samples
 *
 * Mirrors the design of SampleQueue but for OnboardSample structures.
 * Used to decouple the OnboardSampler thread from the main processing thread.
 *
 * Features:
 * - Thread-safe push/pop operations
 * - Blocking pop_wait() for consumer thread
 * - Bounded size to prevent unbounded memory growth
 * - Graceful shutdown via stop()
 */
class OnboardSampleQueue {
public:
    using Sample = OnboardSample;

    OnboardSampleQueue();
    ~OnboardSampleQueue();

    // Non-copyable
    OnboardSampleQueue(const OnboardSampleQueue&) = delete;
    OnboardSampleQueue& operator=(const OnboardSampleQueue&) = delete;

    /**
     * @brief Push a sample to the queue (non-blocking)
     * @param sample Sample to push
     * @return true if pushed successfully, false if queue full or stopped
     */
    bool push(const Sample& sample);

    /**
     * @brief Pop a sample from the queue (non-blocking)
     * @param sample Output parameter for popped sample
     * @return true if sample popped, false if queue empty
     */
    bool pop(Sample& sample);

    /**
     * @brief Pop a sample from the queue (blocking)
     * Waits until a sample is available or stop() is called.
     * @param sample Output parameter for popped sample
     * @return true if sample popped, false if stopped or spurious wakeup
     */
    bool pop_wait(Sample& sample);

    /**
     * @brief Signal the queue to stop
     * Wakes up any threads blocked in pop_wait().
     * After stop(), push() will return false.
     */
    void stop();

    /**
     * @brief Get current queue size
     * @return Number of samples in queue
     */
    size_t size() const;

    /**
     * @brief Check if queue is stopped
     * @return true if stop() has been called
     */
    bool is_stopped() const { return stop_requested_.load(); }

private:
    std::queue<Sample> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_requested_{false};
    size_t max_size_ = 1000000;  // Default 1M sample buffer (~48MB)
};

}  // namespace client
}  // namespace powermonitor
