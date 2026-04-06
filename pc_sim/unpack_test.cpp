#include <gtest/gtest.h>
#include "protocol/unpack.h"

namespace protocol {
namespace {

TEST(UnpackTest, UnpackS20Positive) {
    // Max positive: 0x7FFFF
    uint8_t buf_max[] = {0xFF, 0xFF, 0x07};
    EXPECT_EQ(unpack_s20(buf_max), 524287);

    // One: 1
    uint8_t buf_one[] = {0x01, 0x00, 0x00};
    EXPECT_EQ(unpack_s20(buf_one), 1);

    // Zero: 0
    uint8_t buf_zero[] = {0x00, 0x00, 0x00};
    EXPECT_EQ(unpack_s20(buf_zero), 0);
}

TEST(UnpackTest, UnpackS20Negative) {
    // Max negative: -524288 (0x80000)
    uint8_t buf_min[] = {0x00, 0x00, 0x08};
    EXPECT_EQ(unpack_s20(buf_min), -524288);

    // Minus one: -1 (0xFFFFF)
    uint8_t buf_minus_one[] = {0xFF, 0xFF, 0x0F};
    EXPECT_EQ(unpack_s20(buf_minus_one), -1);
}

TEST(UnpackTest, UnpackS20Robustness) {
    // Positive number with garbage in upper bits of 3rd byte
    // 0x17FFFF -> buf[2] = 0x17. 0x17 & 0x0F = 0x07. Should be 524287.
    uint8_t buf_dirty_pos[] = {0xFF, 0xFF, 0x17};
    EXPECT_EQ(unpack_s20(buf_dirty_pos), 524287);

    // Negative number with garbage in upper bits of 3rd byte
    // 0x180000 -> buf[2] = 0x18. 0x18 & 0x0F = 0x08. Should be -524288.
    uint8_t buf_dirty_neg[] = {0x00, 0x00, 0x18};
    EXPECT_EQ(unpack_s20(buf_dirty_neg), -524288);
}

TEST(UnpackTest, UnpackU20) {
    // Max unsigned: 0xFFFFF (1048575)
    uint8_t buf_max[] = {0xFF, 0xFF, 0x0F};
    EXPECT_EQ(unpack_u20(buf_max), 1048575);

    // Zero
    uint8_t buf_zero[] = {0x00, 0x00, 0x00};
    EXPECT_EQ(unpack_u20(buf_zero), 0);

    // Robustness: Garbage in upper bits
    // 0x1FFFFF -> buf[2] = 0x1F. Should be masked to 0x0F -> 1048575
    uint8_t buf_dirty[] = {0xFF, 0xFF, 0x1F};
    EXPECT_EQ(unpack_u20(buf_dirty), 1048575);
}

TEST(UnpackTest, UnpackS16) {
    // Max positive: 32767 (0x7FFF)
    uint8_t buf_max[] = {0xFF, 0x7F};
    EXPECT_EQ(unpack_s16(buf_max), 32767);

    // Min negative: -32768 (0x8000)
    uint8_t buf_min[] = {0x00, 0x80};
    EXPECT_EQ(unpack_s16(buf_min), -32768);

    // -1: 0xFFFF
    uint8_t buf_minus_one[] = {0xFF, 0xFF};
    EXPECT_EQ(unpack_s16(buf_minus_one), -1);
}

TEST(UnpackTest, ToEngineering) {
    // Check conversions
    // vbus_lsb = 195.3125e-6
    // vshunt_lsb (adcrange=0) = 312.5e-9
    // current_lsb = current_lsb_nA * 1e-9
    // temp_lsb = 7.8125e-3

    uint32_t vbus_raw = 1000;
    int32_t vshunt_raw = 2000;
    int32_t current_raw = 3000;
    int16_t temp_raw = 4000;
    uint32_t power_raw = 5000;
    uint64_t energy_raw = 6000;
    int64_t charge_raw = 7000;
    uint32_t current_lsb_nA = 1000; // 1 uA LSB
    bool adcrange = false;

    EngineeringSample sample = to_engineering(vbus_raw, vshunt_raw, current_raw, temp_raw,
                                              power_raw, energy_raw, charge_raw,
                                              current_lsb_nA, adcrange);

    EXPECT_DOUBLE_EQ(sample.vbus_v, 1000 * 195.3125e-6);
    EXPECT_DOUBLE_EQ(sample.vshunt_v, 2000 * 312.5e-9);
    EXPECT_DOUBLE_EQ(sample.current_a, 3000 * 1000 * 1e-9);
    EXPECT_DOUBLE_EQ(sample.temp_c, 4000 * 7.8125e-3);
    EXPECT_DOUBLE_EQ(sample.power_w, 5000 * 1000 * 1e-9 * 3.2);
    EXPECT_DOUBLE_EQ(sample.energy_j, 6000 * 1000 * 1e-9 * 3.2 * 16.0);
    EXPECT_DOUBLE_EQ(sample.charge_c, 7000 * 1000 * 1e-9);
}

} // namespace
} // namespace protocol
