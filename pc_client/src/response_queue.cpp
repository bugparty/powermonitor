#include "response_queue.h"

#include <chrono>

namespace powermonitor {
namespace client {

void ResponseQueue::push(protocol::Frame&& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(frame));
    cv_.notify_one();
}

bool ResponseQueue::pop_wait(protocol::Frame& frame, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (timeout_ms < 0) {
        // 无限等待
        cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });
    } else {
        // 超时等待
        bool signaled = cv_.wait_for(
            lock, 
            std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty() || stop_requested_; }
        );
        
        if (!signaled && queue_.empty()) {
            return false;  // 超时
        }
    }
    
    if (queue_.empty()) {
        return false;  // 被停止
    }
    
    frame = std::move(queue_.front());
    queue_.pop();
    return true;
}

void ResponseQueue::stop() {
    stop_requested_ = true;
    cv_.notify_all();
}

}  // namespace client
}  // namespace powermonitor
