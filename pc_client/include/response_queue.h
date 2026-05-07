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

    void push(protocol::DynamicFrame&& frame);
    bool pop_wait(protocol::DynamicFrame& frame, int timeout_ms);
    bool pop_by_msgid(protocol::DynamicFrame& frame, uint8_t msgid);
    void stop();

private:
    std::deque<protocol::DynamicFrame> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
};

}  // namespace client
}  // namespace powermonitor
