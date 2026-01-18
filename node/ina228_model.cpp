#include "node/ina228_model.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace node {

namespace {

constexpr double kVbusLsb = 195.3125e-6;
constexpr double kVshuntLsbLow = 312.5e-9;
constexpr double kVshuntLsbHigh = 78.125e-9;
constexpr double kTempLsb = 7.8125e-3;
constexpr double kRshunt = 0.01;
constexpr double kPi = 3.14159265358979323846;

int32_t clamp_s20(int64_t value) {
    return static_cast<int32_t>(std::clamp<int64_t>(value, -524288, 524287));
}

uint32_t clamp_u20(int64_t value) {
    return static_cast<uint32_t>(std::clamp<int64_t>(value, 0, 0xFFFFF));
}

} // namespace

RawSample INA228Model::sample(uint64_t now_us, uint32_t current_lsb_nA, bool adcrange) {
    const double t = static_cast<double>(now_us) / 1e6;
    const double vbus = 12.0 + 0.1 * std::sin(2.0 * kPi * 0.5 * t);
    const double current = 0.5 + 0.2 * std::sin(2.0 * kPi * 0.8 * t + 1.3);
    temp_c_ += 0.001 * std::sin(2.0 * kPi * 0.05 * t);

    const double vshunt = current * kRshunt;

    RawSample out;
    out.vbus_raw = clamp_u20(static_cast<int64_t>(std::llround(vbus / kVbusLsb)));

    const double vshunt_lsb = adcrange ? kVshuntLsbHigh : kVshuntLsbLow;
    out.vshunt_raw = clamp_s20(static_cast<int64_t>(std::llround(vshunt / vshunt_lsb)));

    const double current_lsb = static_cast<double>(current_lsb_nA) * 1e-9;
    const int64_t current_raw = static_cast<int64_t>(std::llround(current / current_lsb));
    out.current_raw = clamp_s20(current_raw);

    out.temp_raw = static_cast<int16_t>(std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(temp_c_ / kTempLsb)), -32768, 32767));

    return out;
}

} // namespace node
