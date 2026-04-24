#ifndef DEVICE_PROTOCOL_FRAME_BUILDER_HPP
#define DEVICE_PROTOCOL_FRAME_BUILDER_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "frame_defs.hpp"
#include "crc16.hpp"

namespace protocol {

// Build a complete protocol frame into the provided buffer.
// Returns the total frame length, or 0 if buffer is too small.
//
// Frame layout:
//   SOF0(0xAA) | SOF1(0x55) | VER | TYPE | FLAGS | SEQ | LEN_L | LEN_H |
//   MSGID | DATA[0..N-1] | CRC_L | CRC_H
//
// Parameters:
//   buf      - Output buffer (must be at least 2 + 6 + 1 + data_len + 2 bytes)
//   buf_size - Size of output buffer
//   type     - Frame type (CMD, RSP, DATA, EVT)
//   flags    - Frame flags
//   seq      - Sequence number
//   msgid    - Message ID
//   data     - Payload data (excluding MSGID), can be nullptr if data_len is 0
//   data_len - Length of payload data
//
// Returns:
//   Total frame length on success, 0 on failure (buffer too small)
inline size_t build_frame(uint8_t* buf, size_t buf_size,
                          FrameType type, uint8_t flags, uint8_t seq,
                          uint8_t msgid, const uint8_t* data, size_t data_len) {
    // LEN = MSGID(1) + data_len
    const uint16_t len = static_cast<uint16_t>(1 + data_len);

    // Total frame size: SOF(2) + Header(6) + Payload(len) + CRC(2)
    const size_t frame_size = 2 + kHeaderSize + len + 2;

    if (buf_size < frame_size) {
        return 0;
    }

    size_t pos = 0;

    // SOF
    buf[pos++] = kSof0;
    buf[pos++] = kSof1;

    // Header: VER, TYPE, FLAGS, SEQ, LEN (LE)
    buf[pos++] = kProtoVersion;
    buf[pos++] = static_cast<uint8_t>(type);
    buf[pos++] = flags;
    buf[pos++] = seq;
    buf[pos++] = static_cast<uint8_t>(len & 0xFF);
    buf[pos++] = static_cast<uint8_t>((len >> 8) & 0xFF);

    // Payload: MSGID + DATA
    buf[pos++] = msgid;
    if (data != nullptr && data_len > 0) {
        memcpy(&buf[pos], data, data_len);
        pos += data_len;
    }

    // CRC16 over VER..DATA (offset 2 to pos-1)
    const uint16_t crc = crc16_ccitt_false(&buf[2], kHeaderSize + len);
    buf[pos++] = static_cast<uint8_t>(crc & 0xFF);
    buf[pos++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return frame_size;
}

// Convenience function to build RSP frame
inline size_t build_rsp(uint8_t* buf, size_t buf_size, uint8_t seq,
                        uint8_t orig_msgid, Status status,
                        const uint8_t* extra_data = nullptr, size_t extra_len = 0) {
    // RSP payload: orig_msgid(1) + status(1) + extra_data
    uint8_t payload[kMaxPayloadLen];
    payload[0] = orig_msgid;
    payload[1] = static_cast<uint8_t>(status);

    size_t payload_len = 2;
    if (extra_data != nullptr && extra_len > 0) {
        memcpy(&payload[2], extra_data, extra_len);
        payload_len += extra_len;
    }

    return build_frame(buf, buf_size, FrameType::kRsp, 0, seq,
                       orig_msgid, payload, payload_len);
}

// Convenience function to build DATA frame
inline size_t build_data(uint8_t* buf, size_t buf_size, uint8_t seq,
                         uint8_t msgid, const uint8_t* data, size_t data_len) {
    return build_frame(buf, buf_size, FrameType::kData, 0, seq,
                       msgid, data, data_len);
}

// Convenience function to build EVT frame
inline size_t build_evt(uint8_t* buf, size_t buf_size, uint8_t seq,
                        uint8_t msgid, const uint8_t* data, size_t data_len) {
    return build_frame(buf, buf_size, FrameType::kEvt, 0, seq,
                       msgid, data, data_len);
}

} // namespace protocol

#endif // DEVICE_PROTOCOL_FRAME_BUILDER_HPP
