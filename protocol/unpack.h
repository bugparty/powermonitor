#pragma once

#include <cstdint>
#include <array>

namespace protocol {

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

struct EngineeringSample {
    double vbus_v = 0.0;
    double vshunt_v = 0.0;
    double current_a = 0.0;
    double temp_c = 0.0;
};

EngineeringSample to_engineering(uint32_t vbus_raw, int32_t vshunt_raw, int32_t current_raw,
                                 int16_t temp_raw, uint32_t current_lsb_nA, bool adcrange);

} // namespace protocol
