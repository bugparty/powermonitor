#include <cstdint>
#include <iostream>
#include <cassert>
#include <vector>

// Define INA228_HPP_MOCK to allow overriding INA228 class
// But sampler.hpp includes "INA228.hpp".
// We need to provide a fake INA228.hpp that defines the class INA228
// So we rely on include path order: mocks/INA228.hpp

// Wait, the plan was to define INA228_HPP to prevent inclusion.
// But then we need to define the class ourselves.
// If sampler.hpp has include guard #ifndef INA228_HPP... #endif
// Then defining it before include works.

#define INA228_HPP 1

// Mock INA228 Class Definition
class INA228 {
public:
    int temp_read_count = 0;

    // Default constructor to match usage in sampler.hpp (pointer usage)
    INA228() = default;

    // Methods called by sampler.hpp
    bool read_diag_alrt(uint16_t& flags) {
        flags = 0;
        return true;
    }
    bool read_vbus_raw(uint32_t& raw) {
        raw = 0x12345;
        return true;
    }
    bool read_vshunt_raw(int32_t& raw) {
        raw = 0x1000;
        return true;
    }
    bool read_current_raw(int32_t& raw) {
        raw = 0x2000;
        return true;
    }

    // The decimation target
    bool read_temp_raw(int16_t& raw) {
        temp_read_count++;
        // Return changing values to verify we use cached value when skipped
        raw = (int16_t)(2500 + temp_read_count);
        return true;
    }
};

// Mock dependencies included by sampler.hpp
// These are handled by include_directories(mocks) in CMakeLists.txt
// But we need to make sure we don't conflict.

// Now include the implementation under test
// This will include "core/shared_context.hpp" which we need.
// And "pico/stdlib.h" etc from mocks.
#include "../sampler.hpp"

// We need to implement the main test logic
int main() {
    // 1. Setup Shared Context
    core::SharedContext shared;
    shared.init();

    // 2. Setup Mock INA228
    INA228 mock_ina;

    // 3. Setup Sampler Context
    // We access the global instance for convenience, or create one if sampler.hpp allows
    // internal linkage of g_sampler_ctx might mean we have our own copy here.
    // Let's use the one in sampler.hpp
    using namespace device;

    // Manually initialize context as sampler_init would
    g_sampler_ctx.shared = &shared;
    g_sampler_ctx.ina228 = &mock_ina;
    g_sampler_ctx.timer_active = true;
    g_sampler_ctx.isr_seq = 0;
    g_sampler_ctx.worker_seq = 0;

    // Initialize the new decimation fields (simulating sampler_init)
    // NOTE: These fields don't exist yet in sampler.hpp!
    // We are writing the test FIRST (TDD), so this compilation WILL FAIL until we modify sampler.hpp.
    // This is expected.
    g_sampler_ctx.dietemp_skip_counter = 0;
    g_sampler_ctx.last_dietemp_raw = 0;

    std::cout << "Starting Decimation Test..." << std::endl;

    // Run 20 iterations
    const int kIterations = 20;
    for(int i=0; i<kIterations; ++i) {
        // Simulate ISR increment
        g_sampler_ctx.isr_seq++;

        // Run worker
        sampler_do_work(&g_sampler_ctx);

        // Verify samples produced
        if (shared.samples_produced != (uint32_t)(i + 1)) {
            std::cerr << "Error: Sample not produced at iteration " << i << std::endl;
            return 1;
        }

        // Verify Sample Content (optional, to check if temp is updating/holding)
        // We can't easily peek into the queue without popping.
        // But we can check mock_ina.temp_read_count
    }

    std::cout << "Total Samples: " << shared.samples_produced << std::endl;
    std::cout << "Total Temp Reads: " << mock_ina.temp_read_count << std::endl;

    // Verification
    // Iteration 0: read (count=1), skip_counter becomes 9
    // Iteration 1..9: skip (count=1), skip_counter decrements 8..0
    // Iteration 10: read (count=2), skip_counter becomes 9
    // Iteration 11..19: skip (count=2)

    if (mock_ina.temp_read_count != 2) {
        std::cerr << "FAIL: Expected 2 temperature reads, got " << mock_ina.temp_read_count << std::endl;
        return 1;
    }

    std::cout << "PASS: Temperature decimation working correctly." << std::endl;
    return 0;
}
