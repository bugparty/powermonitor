#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>

namespace powermonitor {
namespace client {

class SampleQueue {
public:
    struct Sample {
        uint32_t seq = 0;
        uint64_t host_timestamp_us = 0;
        std::vector<uint8_t> raw_data;
        uint32_t device_timestamp_us = 0;
        uint64_t device_timestamp_unix_us = 0;  // Absolute timestamp (Unix us)
    };

    SampleQueue() = default;
    ~SampleQueue() = default;

    SampleQueue(const SampleQueue&) = delete;
    SampleQueue& operator=(const SampleQueue&) = delete;

    bool push(Sample&& sample);
    bool pop(Sample& sample);
    bool pop_wait(Sample& sample);
    void stop();
    size_t size() const;
    bool is_stopped() const { return stop_requested_.load(); }

private:
    std::queue<Sample> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_requested_{false};
    size_t max_size_ = 1000000;  // Default 1M sample buffer
};

}  // namespace client
}  // namespace powermonitor
