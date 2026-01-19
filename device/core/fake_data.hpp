#ifndef DEVICE_CORE_FAKE_DATA_HPP
#define DEVICE_CORE_FAKE_DATA_HPP

#include "raw_sample.hpp"

namespace core {

// Fake data generator for Phase 1 protocol debugging
// Uses incrementing counter for easy packet loss detection
class FakeDataGenerator {
public:
    FakeDataGenerator() = default;

    // Reset counter and timestamp origin
    void reset() {
        counter_ = 0;
    }

    // Generate next fake sample
    // Counter is embedded in the low bits of each measurement
    RawSample next() {
        RawSample s;

        // Timestamp: counter * 1000us (simulating 1kHz sampling)
        s.timestamp_us = counter_ * 1000;

        // VBUS: ~12V base + counter in low 8 bits
        // 12V / 195.3125uV = 0x0F000 (61440)
        s.vbus_raw = 0x0F000 + (counter_ & 0xFF);

        // VSHUNT: small base + counter in low 8 bits
        s.vshunt_raw = 0x00100 + static_cast<int32_t>(counter_ & 0xFF);

        // CURRENT: ~0.5A equivalent + counter in low 8 bits
        // Depends on current_lsb, using 0x01000 as reasonable base
        s.current_raw = 0x01000 + static_cast<int32_t>(counter_ & 0xFF);

        // DIE_TEMP: ~35°C + small variation
        // 35°C / 7.8125m°C = 4480
        s.dietemp_raw = static_cast<int16_t>(4480 + (counter_ & 0x0F));

        // Flags: CAL_VALID always set, CNVRF set
        s.flags = SampleFlags::kCalValid | SampleFlags::kCnvrf;

        s._pad = 0;

        ++counter_;
        return s;
    }

    // Get current counter value (for debugging)
    uint32_t counter() const { return counter_; }

private:
    uint32_t counter_ = 0;
};

} // namespace core

#endif // DEVICE_CORE_FAKE_DATA_HPP
