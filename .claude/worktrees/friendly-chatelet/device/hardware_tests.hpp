#ifndef DEVICE_HARDWARE_TESTS_HPP
#define DEVICE_HARDWARE_TESTS_HPP

namespace device {
struct DeviceContext;
}
namespace core {
struct SharedContext;
}

namespace device {

/// Run pack correctness/timing, INA228 self-test, and PIO I2C benchmarks.
/// No-op when POWERMONITOR_TEST_MODE is not defined.
void run_hardware_tests(DeviceContext& ctx, core::SharedContext& shared_ctx);

}  // namespace device

#endif  // DEVICE_HARDWARE_TESTS_HPP
