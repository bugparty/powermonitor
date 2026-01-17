#include "protocol/parser.h"

#include <algorithm>

#include "protocol/crc16_ccitt_false.h"

namespace protocol {

Parser::Parser(FrameCallback callback, uint16_t max_len)
    : callback_(std::move(callback)), max_len_(max_len) {}

void Parser::feed(const uint8_t *data, size_t len) {
    buffer_.insert(buffer_.end(), data, data + len);
    process();
}

void Parser::feed(const std::vector<uint8_t> &data) {
    feed(data.data(), data.size());
}

void Parser::process() {
    bool progressed = true;
    while (progressed) {
        progressed = false;
        switch (state_) {
        case State::kWaitSof0: {
            auto it = std::find(buffer_.begin(), buffer_.end(), kSof0);
            if (it == buffer_.end()) {
                buffer_.clear();
                return;
            }
            if (it != buffer_.begin()) {
                buffer_.erase(buffer_.begin(), it);
            }
            if (buffer_.size() < 2) {
                return;
            }
            state_ = State::kWaitSof1;
            progressed = true;
            break;
        }
        case State::kWaitSof1:
            if (buffer_.size() < 2) {
                return;
            }
            if (buffer_[1] == kSof1) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
                state_ = State::kReadHeader;
                progressed = true;
            } else if (buffer_[1] == kSof0) {
                buffer_.erase(buffer_.begin());
                progressed = true;
            } else {
                buffer_.erase(buffer_.begin());
                state_ = State::kWaitSof0;
                progressed = true;
            }
            break;
        case State::kReadHeader:
            if (buffer_.size() < 6) {
                return;
            }
            read_header();
            progressed = true;
            break;
        case State::kReadPayload:
            if (buffer_.size() < payload_remaining_) {
                return;
            }
            read_payload();
            progressed = true;
            break;
        }
    }
}

void Parser::read_header() {
    current_frame_.ver = buffer_[0];
    current_frame_.type = static_cast<FrameType>(buffer_[1]);
    current_frame_.flags = buffer_[2];
    current_frame_.seq = buffer_[3];
    current_frame_.len = static_cast<uint16_t>(buffer_[4]) |
                         (static_cast<uint16_t>(buffer_[5]) << 8U);
    buffer_.erase(buffer_.begin(), buffer_.begin() + 6);
    if (current_frame_.len == 0 || current_frame_.len > max_len_) {
        ++len_fail_count_;
        state_ = State::kWaitSof0;
        return;
    }
    payload_remaining_ = static_cast<uint16_t>(current_frame_.len + 2);
    state_ = State::kReadPayload;
}

void Parser::read_payload() {
    const size_t payload_len = current_frame_.len;
    std::vector<uint8_t> payload(buffer_.begin(), buffer_.begin() + payload_len);
    const uint8_t crc_l = buffer_[payload_len];
    const uint8_t crc_h = buffer_[payload_len + 1];
    buffer_.erase(buffer_.begin(), buffer_.begin() + payload_len + 2);

    std::vector<uint8_t> crc_range;
    crc_range.reserve(6 + payload_len);
    crc_range.push_back(current_frame_.ver);
    crc_range.push_back(static_cast<uint8_t>(current_frame_.type));
    crc_range.push_back(current_frame_.flags);
    crc_range.push_back(current_frame_.seq);
    crc_range.push_back(static_cast<uint8_t>(current_frame_.len & 0xFF));
    crc_range.push_back(static_cast<uint8_t>((current_frame_.len >> 8) & 0xFF));
    crc_range.insert(crc_range.end(), payload.begin(), payload.end());

    const uint16_t expected = crc16_ccitt_false(crc_range.data(), crc_range.size());
    const uint16_t actual = static_cast<uint16_t>(crc_l) | (static_cast<uint16_t>(crc_h) << 8U);
    if (expected != actual) {
        ++crc_fail_count_;
        state_ = State::kWaitSof0;
        return;
    }

    current_frame_.msgid = payload[0];
    current_frame_.data.assign(payload.begin() + 1, payload.end());
    if (callback_) {
        callback_(current_frame_);
    }
    state_ = State::kWaitSof0;
}

} // namespace protocol
