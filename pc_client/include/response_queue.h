#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

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
    bool pop_by_msgid(protocol::Frame& frame, uint8_t msgid);
    void stop();

private:
    std::deque<protocol::Frame> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
};

}  // namespace client
}  // namespace powermonitor
