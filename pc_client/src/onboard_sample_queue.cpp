#include "session.h"  // For OnboardSample definition - must be first
#include "onboard_sample_queue.h"

namespace powermonitor {
namespace client {

OnboardSampleQueue::OnboardSampleQueue() = default;

OnboardSampleQueue::~OnboardSampleQueue() = default;

bool OnboardSampleQueue::push(const Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);

    // If queue is full, drop oldest sample
    if (queue_.size() >= max_size_) {
        queue_.pop();
        queue_.push(sample);
        cv_.notify_one();
        return false;  // Indicates sample was dropped
    }

    queue_.push(sample);
    cv_.notify_one();
    return true;
}

bool OnboardSampleQueue::pop(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    sample = queue_.front();
    queue_.pop();
    return true;
}

bool OnboardSampleQueue::pop_wait(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait until queue has data or stop is requested
    cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });

    if (queue_.empty()) {
        return false;  // Stopped or spurious wakeup
    }

    sample = queue_.front();
    queue_.pop();
    return true;
}

void OnboardSampleQueue::stop() {
    stop_requested_ = true;
    cv_.notify_all();  // Wake up all waiting threads
}

size_t OnboardSampleQueue::size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace client
}  // namespace powermonitor
