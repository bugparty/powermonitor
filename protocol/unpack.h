#pragma once

#include <cstdint>
#include <array>

namespace protocol {

uint32_t unpack_u20(const uint8_t buf[3]);
int32_t unpack_s20(const uint8_t buf[3]);
int16_t unpack_s16(const uint8_t buf[2]);

struct EngineeringSample {
    double vbus_v = 0.0;
    double vshunt_v = 0.0;
    double current_a = 0.0;
    double temp_c = 0.0;
};

EngineeringSample to_engineering(uint32_t vbus_raw, int32_t vshunt_raw, int32_t current_raw,
                                 int16_t temp_raw, uint32_t current_lsb_nA, bool adcrange);

} // namespace protocol
