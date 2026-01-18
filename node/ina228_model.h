#pragma once

#include <cstdint>

namespace node {

struct RawSample {
    uint32_t vbus_raw = 0;
    int32_t vshunt_raw = 0;
    int32_t current_raw = 0;
    int16_t temp_raw = 0;
};

class INA228Model {
public:
    RawSample sample(uint64_t now_us, uint32_t current_lsb_nA, bool adcrange);

private:
    double temp_c_ = 35.0;
};

} // namespace node
