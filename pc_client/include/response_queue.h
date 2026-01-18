#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

#include "protocol/frame_builder.h"

namespace powermonitor {
namespace client {

class ResponseQueue {
public:
    ResponseQueue() = default;
    ~ResponseQueue() = default;

    ResponseQueue(const ResponseQueue&) = delete;
    ResponseQueue& operator=(const ResponseQueue&) = delete;

    void push(protocol::Frame&& frame);
    bool pop_wait(protocol::Frame& frame, int timeout_ms);
    void stop();
    
private:
    std::queue<protocol::Frame> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
};

}  // namespace client
}  // namespace powermonitor
