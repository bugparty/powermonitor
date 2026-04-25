#include "sample_queue.h"

namespace powermonitor {
namespace client {

bool SampleQueue::push(Sample&& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_) {
        queue_.pop();
        queue_.push(std::move(sample));
        cv_.notify_one();
        return false;
    }
    queue_.push(std::move(sample));
    cv_.notify_one();
    return true;
}

bool SampleQueue::pop(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    sample = std::move(queue_.front());
    queue_.pop();
    return true;
}

bool SampleQueue::pop_wait(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });
    if (queue_.empty()) {
        return false;
    }
    sample = std::move(queue_.front());
    queue_.pop();
    return true;
}

void SampleQueue::stop() {
    stop_requested_ = true;
    cv_.notify_all();
}

size_t SampleQueue::size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace client
}  // namespace powermonitor
