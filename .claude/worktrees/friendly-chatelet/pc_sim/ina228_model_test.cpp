#include <gtest/gtest.h>
#include "node/ina228_model.h"

namespace node {

class INA228ModelTest : public ::testing::Test {
protected:
    INA228Model model;
};

// Test basic sample output with sufficient LSB to avoid clamping
TEST_F(INA228ModelTest, SampleReturnsReasonableValues) {
    // 10000 nA = 10 uA LSB
    // Max current ~0.7 A. 0.7 / 10e-6 = 70,000. Fits in 20-bit signed (524287).
    RawSample sample = model.sample(0, 10000, false);

    // Check VBUS (around 12V)
    // kVbusLsb = 195.3125e-6
    // 12 / 195.3125e-6 = 61440
    // Allow small margin due to potential rounding
    EXPECT_NEAR(sample.vbus_raw, 61440, 10);

    // Check Temp (around 35C)
    // kTempLsb = 7.8125e-3
    // 35 / 7.8125e-3 = 4480
    EXPECT_NEAR(sample.temp_raw, 4480, 10);

    // Check Current
    // t=0, current ~ 0.6927 A
    // 0.6927 / 10e-6 = 69270
    EXPECT_NEAR(sample.current_raw, 69270, 1000); // Allow loose check as sin(1.3) is approx
}

TEST_F(INA228ModelTest, CurrentClamping) {
    // Current is around 0.5A + sine wave. At t=0 it is ~0.69 A.
    // Use 1000 nA = 1 uA LSB.
    // 0.69 / 1e-6 = 690,000 > 524287 (max 20-bit signed)

    RawSample sample = model.sample(0, 1000, false);

    // Should be clamped to max positive 20-bit signed integer
    EXPECT_EQ(sample.current_raw, 524287);
}

TEST_F(INA228ModelTest, AdcRangeAffectsVShunt) {
    // Use 10000 nA LSB to avoid current clamping affecting vshunt indirectly?
    // No, vshunt is calculated from current, not current_raw.

    RawSample sample_low = model.sample(0, 10000, false);
    RawSample sample_high = model.sample(0, 10000, true);

    // vshunt_raw = vshunt / vshunt_lsb
    // vshunt_lsb_low = 312.5e-9
    // vshunt_lsb_high = 78.125e-9
    // Ratio: 312.5 / 78.125 = 4.0
    // So sample_high.vshunt_raw should be 4x sample_low.vshunt_raw

    // Ensure not 0
    ASSERT_NE(sample_low.vshunt_raw, 0);

    double ratio = static_cast<double>(sample_high.vshunt_raw) / sample_low.vshunt_raw;
    EXPECT_NEAR(ratio, 4.0, 0.1);
}

} // namespace node
