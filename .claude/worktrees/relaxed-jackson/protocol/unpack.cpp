#include "protocol/unpack.h"

namespace protocol {

EngineeringSample to_engineering(uint32_t vbus_raw, int32_t vshunt_raw, int32_t current_raw,
                                 int16_t temp_raw, uint32_t current_lsb_nA, bool adcrange) {
    const double vbus_lsb = 195.3125e-6;
    const double vshunt_lsb = adcrange ? 78.125e-9 : 312.5e-9;
    const double current_lsb = static_cast<double>(current_lsb_nA) * 1e-9;
    const double temp_lsb = 7.8125e-3;
    EngineeringSample sample;
    sample.vbus_v = static_cast<double>(vbus_raw) * vbus_lsb;
    sample.vshunt_v = static_cast<double>(vshunt_raw) * vshunt_lsb;
    sample.current_a = static_cast<double>(current_raw) * current_lsb;
    sample.temp_c = static_cast<double>(temp_raw) * temp_lsb;
    return sample;
}

} // namespace protocol
