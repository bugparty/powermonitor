#ifndef PROTOCOL_PROTOCOL_CONSTANTS_H
#define PROTOCOL_PROTOCOL_CONSTANTS_H

#include <cstdint>

namespace protocol {

// Start of Frame bytes
constexpr uint8_t kSof0 = 0xAA;
constexpr uint8_t kSof1 = 0x55;

// Protocol version
constexpr uint8_t kProtoVersion = 0x01;

// Frame type (TYPE field)
enum class FrameType : uint8_t {
    kCmd  = 0x01,  // PC -> Device: Control command
    kRsp  = 0x02,  // Device -> PC: Command response
    kData = 0x03,  // Device -> PC: Data stream (no ACK)
    kEvt  = 0x04,  // Device -> PC: Async event
};

// Message IDs (MSGID field)
enum class MsgId : uint8_t {
    // Management
    kPing        = 0x01,
    kTimeSync    = 0x05,
    kTimeAdjust  = 0x06,
    kTimeSet     = 0x07,

    // Configuration
    kSetCfg      = 0x10,
    kGetCfg      = 0x11,

    // Debug
    kRegRead     = 0x20,
    kRegWrite    = 0x21,

    // Stream control
    kStreamStart = 0x30,
    kStreamStop  = 0x31,

    // Data
    kDataSample  = 0x80,

    // Events
    kEvtAlert    = 0x90,
    kCfgReport   = 0x91,
    kStatsReport = 0x92,
    kTextReport      = 0x93,
    kTimeSyncRequest = 0x94,
};

// Response status codes
enum class Status : uint8_t {
    kOk         = 0x00,  // Success
    kErrCrc     = 0x01,  // CRC verification failed
    kErrLen     = 0x02,  // Invalid packet length
    kErrUnkCmd  = 0x03,  // Unknown or unsupported MSGID
    kErrParam   = 0x04,  // Parameter out of range
    kErrHw      = 0x05,  // Hardware fault (e.g., I2C NACK)
};

} // namespace protocol

#endif // PROTOCOL_PROTOCOL_CONSTANTS_H
