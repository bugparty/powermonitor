#pragma once

#include <cstdint>
#include <vector>

namespace powermonitor {
namespace client {

// Helper functions to patch missing protocol APIs

inline uint32_t unpack_u32(const uint8_t* buf) {
    return static_cast<uint32_t>(buf[0]) |
           (static_cast<uint32_t>(buf[1]) << 8) |
           (static_cast<uint32_t>(buf[2]) << 16) |
           (static_cast<uint32_t>(buf[3]) << 24);
}

inline uint64_t unpack_u64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

inline int32_t unpack_s32(const uint8_t* buf) {
    uint32_t raw = unpack_u32(buf);
    return static_cast<int32_t>(raw);
}

inline uint16_t unpack_u16(const uint8_t* buf) {
    return static_cast<uint16_t>(buf[0]) |
           (static_cast<uint16_t>(buf[1]) << 8);
}

inline int16_t unpack_s16(const uint8_t* buf) {
    uint16_t raw = unpack_u16(buf);
    return static_cast<int16_t>(raw);
}

inline void pack_u16(uint8_t* buf, uint16_t value) {
    buf[0] = static_cast<uint8_t>(value & 0xFF);
    buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void pack_u32(uint8_t* buf, uint32_t value) {
    buf[0] = static_cast<uint8_t>(value & 0xFF);
    buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

}  // namespace client
}  // namespace powermonitor
