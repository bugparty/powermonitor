#ifndef DEVICE_PROTOCOL_PARSER_HPP
#define DEVICE_PROTOCOL_PARSER_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "frame_defs.hpp"
#include "crc16.hpp"

namespace protocol {

// Frame callback function pointer type
// Called when a complete, valid frame is received
using FrameCallback = void (*)(const Frame& frame, void* user_data);

// Protocol parser with 4-state FSM
// Uses fixed-size buffer, no dynamic allocation
class Parser {
public:
    // Parser states
    enum class State {
        kWaitSof0,    // Waiting for first start byte (0xAA)
        kWaitSof1,    // Waiting for second start byte (0x55)
        kReadHeader,  // Reading 6-byte header
        kReadPayload  // Reading payload + CRC
    };

    Parser() = default;

    // Set callback for received frames
    void set_callback(FrameCallback cb, void* user_data = nullptr) {
        callback_ = cb;
        user_data_ = user_data;
    }

    // Feed data into the parser (can be called with partial data)
    void feed(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            feed_byte(data[i]);
        }
    }

    // Feed a single byte
    void feed_byte(uint8_t byte) {
        switch (state_) {
        case State::kWaitSof0:
            if (byte == kSof0) {
                state_ = State::kWaitSof1;
            }
            break;

        case State::kWaitSof1:
            if (byte == kSof1) {
                buf_pos_ = 0;
                state_ = State::kReadHeader;
            } else if (byte == kSof0) {
                // Stay in kWaitSof1, treat as new SOF0
            } else {
                state_ = State::kWaitSof0;
            }
            break;

        case State::kReadHeader:
            header_buf_[buf_pos_++] = byte;
            if (buf_pos_ >= kHeaderSize) {
                parse_header();
            }
            break;

        case State::kReadPayload:
            payload_buf_[buf_pos_++] = byte;
            if (buf_pos_ >= payload_total_) {
                verify_and_dispatch();
            }
            break;
        }
    }

    // Reset parser state
    void reset() {
        state_ = State::kWaitSof0;
        buf_pos_ = 0;
        crc_fail_count_ = 0;
        len_fail_count_ = 0;
    }

    // Accessors for statistics
    uint32_t crc_fail_count() const { return crc_fail_count_; }
    uint32_t len_fail_count() const { return len_fail_count_; }
    State state() const { return state_; }

private:
    void parse_header() {
        current_frame_.ver = header_buf_[0];
        current_frame_.type = static_cast<FrameType>(header_buf_[1]);
        current_frame_.flags = header_buf_[2];
        current_frame_.seq = header_buf_[3];
        current_frame_.len = static_cast<uint16_t>(header_buf_[4]) |
                             (static_cast<uint16_t>(header_buf_[5]) << 8);

        // Validate length
        if (current_frame_.len == 0 || current_frame_.len > kMaxPayloadLen) {
            ++len_fail_count_;
            state_ = State::kWaitSof0;
            return;
        }

        // Payload includes MSGID + DATA + CRC(2)
        payload_total_ = current_frame_.len + 2;
        buf_pos_ = 0;
        state_ = State::kReadPayload;
    }

    void verify_and_dispatch() {
        // Extract CRC from end of payload buffer
        const size_t payload_len = current_frame_.len;
        const uint8_t crc_l = payload_buf_[payload_len];
        const uint8_t crc_h = payload_buf_[payload_len + 1];
        const uint16_t received_crc = static_cast<uint16_t>(crc_l) |
                                      (static_cast<uint16_t>(crc_h) << 8);

        // Compute CRC incrementally over header then payload to avoid
        // a large stack buffer (kHeaderSize + kMaxPayloadLen was ~4103 bytes).
        const uint16_t computed_crc = crc16_ccitt_false_two(
            header_buf_, kHeaderSize, payload_buf_, payload_len);

        if (computed_crc != received_crc) {
            ++crc_fail_count_;
            // Try to resync by scanning for SOF in payload buffer
            resync();
            return;
        }

        // Extract MSGID and data
        current_frame_.msgid = payload_buf_[0];
        current_frame_.data_len = static_cast<uint16_t>(payload_len - 1);
        if (current_frame_.data_len > 0) {
            memcpy(current_frame_.data, &payload_buf_[1], current_frame_.data_len);
        }

        // Dispatch to callback
        if (callback_ != nullptr) {
            callback_(current_frame_, user_data_);
        }

        state_ = State::kWaitSof0;
    }

    void resync() {
        // Scan payload buffer for potential SOF sequence
        // This handles the case where a new frame starts within corrupted data
        for (size_t i = 0; i < payload_total_ - 1; ++i) {
            if (payload_buf_[i] == kSof0 && payload_buf_[i + 1] == kSof1) {
                // Found potential SOF, feed remaining bytes
                buf_pos_ = 0;
                state_ = State::kReadHeader;
                for (size_t j = i + 2; j < payload_total_; ++j) {
                    feed_byte(payload_buf_[j]);
                }
                return;
            }
        }
        // No SOF found, go back to waiting
        state_ = State::kWaitSof0;
    }

    FrameCallback callback_ = nullptr;
    void* user_data_ = nullptr;

    State state_ = State::kWaitSof0;
    Frame current_frame_{};

    // Header buffer (6 bytes)
    uint8_t header_buf_[kHeaderSize]{};

    // Payload buffer (payload + CRC)
    uint8_t payload_buf_[kMaxPayloadLen + 2]{};

    size_t buf_pos_ = 0;
    size_t payload_total_ = 0;

    uint32_t crc_fail_count_ = 0;
    uint32_t len_fail_count_ = 0;
};

} // namespace protocol

#endif // DEVICE_PROTOCOL_PARSER_HPP
