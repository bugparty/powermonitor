#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace protocol {

// Append helpers using resize() optimization
inline void append_u16(std::vector<uint8_t> &out, uint16_t value) {
    size_t idx = out.size();
    out.resize(idx + 2);
    out[idx] = static_cast<uint8_t>(value & 0xFF);
    out[idx + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void append_u32(std::vector<uint8_t> &out, uint32_t value) {
    size_t idx = out.size();
    out.resize(idx + 4);
    out[idx] = static_cast<uint8_t>(value & 0xFF);
    out[idx + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[idx + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[idx + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline void append_u64(std::vector<uint8_t> &out, uint64_t value) {
    size_t idx = out.size();
    out.resize(idx + 8);
    out[idx] = static_cast<uint8_t>(value & 0xFF);
    out[idx + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[idx + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[idx + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    out[idx + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    out[idx + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    out[idx + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    out[idx + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

inline void append_i64(std::vector<uint8_t> &out, int64_t value) {
    append_u64(out, static_cast<uint64_t>(value));
}

// Read helpers
inline uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8U);
}

inline uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<uint32_t>(data[offset + 3]) << 24U);
}

inline uint64_t read_u64(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint64_t>(data[offset]) |
           (static_cast<uint64_t>(data[offset + 1]) << 8U) |
           (static_cast<uint64_t>(data[offset + 2]) << 16U) |
           (static_cast<uint64_t>(data[offset + 3]) << 24U) |
           (static_cast<uint64_t>(data[offset + 4]) << 32U) |
           (static_cast<uint64_t>(data[offset + 5]) << 40U) |
           (static_cast<uint64_t>(data[offset + 6]) << 48U) |
           (static_cast<uint64_t>(data[offset + 7]) << 56U);
}

inline int64_t read_i64(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<int64_t>(read_u64(data, offset));
}

// 20-bit packing
inline void pack_u20(uint32_t value, uint8_t out[3]) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 16) & 0x0F);
}

inline void pack_s20(int32_t value, uint8_t out[3]) {
    const uint32_t raw = static_cast<uint32_t>(value) & 0xFFFFF;
    out[0] = static_cast<uint8_t>(raw & 0xFF);
    out[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((raw >> 16) & 0x0F);
}

// 20-bit unpacking
inline uint32_t unpack_u20(const uint8_t buf[3]) {
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8U) |
           ((static_cast<uint32_t>(buf[2]) & 0x0F) << 16U);
}

inline int32_t unpack_s20(const uint8_t buf[3]) {
    uint32_t raw = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8U) |
                   ((static_cast<uint32_t>(buf[2]) & 0x0F) << 16U);
    if (raw & 0x80000U) {
        raw |= 0xFFF00000U;
    }
    return static_cast<int32_t>(raw);
}

inline int16_t unpack_s16(const uint8_t buf[2]) {
    uint16_t raw = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8U);
    return static_cast<int16_t>(raw);
}

} // namespace protocol
