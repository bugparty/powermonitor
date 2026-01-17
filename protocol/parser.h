#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "protocol/frame_builder.h"

namespace protocol {

class Parser {
public:
    using FrameCallback = std::function<void(const Frame &)>;

    explicit Parser(FrameCallback callback, uint16_t max_len = 1024);

    void feed(const uint8_t *data, size_t len);
    void feed(const std::vector<uint8_t> &data);

    uint64_t crc_fail_count() const { return crc_fail_count_; }
    uint64_t len_fail_count() const { return len_fail_count_; }

private:
    enum class State {
        kWaitSof0,
        kWaitSof1,
        kReadHeader,
        kReadPayload
    };

    void process();
    void read_header();
    void read_payload();

    FrameCallback callback_;
    std::vector<uint8_t> buffer_;
    State state_ = State::kWaitSof0;
    Frame current_frame_{};
    uint16_t max_len_ = 1024;
    uint16_t payload_remaining_ = 0;
    uint64_t crc_fail_count_ = 0;
    uint64_t len_fail_count_ = 0;
};

} // namespace protocol
