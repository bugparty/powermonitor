#pragma once

#include <cstdint>
#include <vector>

namespace protocol {

constexpr uint8_t kSof0 = 0xAA;
constexpr uint8_t kSof1 = 0x55;
constexpr uint8_t kProtoVersion = 0x01;

enum class FrameType : uint8_t {
    kCmd = 0x01,
    kRsp = 0x02,
    kData = 0x03,
    kEvt = 0x04,
};

// Message IDs (MSGID field)
enum class MsgId : uint8_t {
    // Management
    kPing = 0x01,

    // Configuration
    kSetCfg = 0x10,
    kGetCfg = 0x11,

    // Debug
    kRegRead = 0x20,
    kRegWrite = 0x21,

    // Stream control
    kStreamStart = 0x30,
    kStreamStop = 0x31,

    // Data
    kDataSample = 0x80,

    // Events
    kEvtAlert = 0x90,
    kCfgReport = 0x91,
    kTextReport = 0x93,
};

// Response status codes
enum class Status : uint8_t {
    kOk = 0x00,        // Success
    kErrCrc = 0x01,    // CRC verification failed
    kErrLen = 0x02,    // Invalid packet length
    kErrUnkCmd = 0x03, // Unknown or unsupported MSGID
    kErrParam = 0x04,  // Parameter out of range
    kErrHw = 0x05,     // Hardware fault (e.g., I2C NACK)
};

struct Frame {
    uint8_t ver = kProtoVersion;
    FrameType type = FrameType::kCmd;
    uint8_t flags = 0;
    uint8_t seq = 0;
    uint16_t len = 0;
    uint8_t msgid = 0;
    std::vector<uint8_t> data;
};

// Build a complete protocol frame with SOF, header, payload, and CRC16.
std::vector<uint8_t> build_frame(FrameType type, uint8_t flags, uint8_t seq, uint8_t msgid,
                                 const std::vector<uint8_t> &data);

} // namespace protocol
