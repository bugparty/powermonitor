#ifndef DEVICE_CORE_RAW_SAMPLE_HPP
#define DEVICE_CORE_RAW_SAMPLE_HPP

#include <cstdint>

namespace core {

// Compact representation of one sensor reading (28 bytes)
// Used for inter-core communication via ring buffer
// Field order: 8-byte first for alignment, then 4-byte, 2-byte, 1-byte
struct RawSample {
    uint64_t timestamp_unix_us;      // Absolute Unix timestamp (sampling time + epoch_offset)
    uint32_t timestamp_us;           // Relative to stream start (sampling time)
    uint32_t vbus_raw;              // 20-bit unsigned raw value (in low 20 bits)
    int32_t vshunt_raw;             // 20-bit signed raw
    int32_t current_raw;            // 20-bit signed raw
    int16_t dietemp_raw;            // 16-bit signed raw
    uint8_t flags;                  // See SampleFlags namespace
    uint8_t _pad;                  // Padding (for 28 bytes total)
};

// 28 bytes data + potential tail padding for alignment
static_assert(sizeof(RawSample) >= 28, "RawSample must be at least 28 bytes");

// Flag bit definitions
namespace SampleFlags {
    constexpr uint8_t kCnvrf    = 0x01;  // ADC conversion complete
    constexpr uint8_t kAlert    = 0x02;  // Alert triggered
    constexpr uint8_t kCalValid = 0x04;  // Calibration valid (CURRENT is valid)
    constexpr uint8_t kMathOvf  = 0x08;  // Math overflow (MATHOF from DIAG_ALRT)
    constexpr uint8_t kI2cError = 0x10;  // I2C read error occurred
}

// Pack a 20-bit unsigned value into 3-byte LE format
inline void pack_u20(uint8_t* buf, uint32_t value) {
    buf[0] = static_cast<uint8_t>(value & 0xFF);
    buf[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((value >> 16) & 0x0F);
}

// Pack a 20-bit signed value into 3-byte LE format
inline void pack_s20(uint8_t* buf, int32_t value) {
    // Convert to 20-bit two's complement
    uint32_t raw = static_cast<uint32_t>(value) & 0xFFFFF;
    buf[0] = static_cast<uint8_t>(raw & 0xFF);
    buf[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((raw >> 16) & 0x0F);
}

} // namespace core

#endif // DEVICE_CORE_RAW_SAMPLE_HPP
