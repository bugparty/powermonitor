#pragma once

#include "protocol/serialization.h"

namespace protocol {

struct EngineeringSample {
    double vbus_v = 0.0;
    double vshunt_v = 0.0;
    double current_a = 0.0;
    double temp_c = 0.0;
    double power_w = 0.0;
    double energy_j = 0.0;
    double charge_c = 0.0;
};

EngineeringSample to_engineering(uint32_t vbus_raw, int32_t vshunt_raw, int32_t current_raw,
                                 int16_t temp_raw, uint32_t power_raw, uint64_t energy_raw, int64_t charge_raw,
                                 uint32_t current_lsb_nA, bool adcrange);

} // namespace protocol
