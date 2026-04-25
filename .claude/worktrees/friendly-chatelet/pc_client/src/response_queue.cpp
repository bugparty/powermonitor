#include "response_queue.h"

#include <chrono>

namespace powermonitor {
namespace client {

void ResponseQueue::push(protocol::Frame&& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(frame));
    cv_.notify_one();
}

bool ResponseQueue::pop_wait(protocol::Frame& frame, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (timeout_ms < 0) {
        // Wait indefinitely
        cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });
    } else {
        // Wait with timeout
        bool signaled = cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty() || stop_requested_; }
        );

        if (!signaled && queue_.empty()) {
            return false;  // Timeout
        }
    }

    if (queue_.empty()) {
        return false;  // Stopped
    }

    frame = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

bool ResponseQueue::pop_by_msgid(protocol::Frame& frame, uint8_t msgid) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->msgid == msgid) {
            frame = std::move(*it);
            queue_.erase(it);
            return true;
        }
    }
    return false;
}

void ResponseQueue::stop() {
    stop_requested_ = true;
    cv_.notify_all();
}

}  // namespace client
}  // namespace powermonitor
