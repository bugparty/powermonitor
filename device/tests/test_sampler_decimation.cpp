#include <iostream>
#include <cassert>
#include <vector>
#include <cstdint>

// Mock definition for INA228 to avoid including the real one
#define INA228_HPP

class INA228 {
public:
    int read_temp_count = 0;
    int read_vbus_count = 0;
    int read_vshunt_count = 0;
    int read_current_count = 0;

    bool read_diag_alrt(uint16_t& flags) const { flags = 0; return true; }
    bool read_vbus_raw(uint32_t& raw) const { const_cast<INA228*>(this)->read_vbus_count++; raw = 0; return true; }
    bool read_vshunt_raw(int32_t& raw) const { const_cast<INA228*>(this)->read_vshunt_count++; raw = 0; return true; }
    bool read_current_raw(int32_t& raw) const { const_cast<INA228*>(this)->read_current_count++; raw = 0; return true; }
    bool read_temp_raw(int16_t& raw) const { const_cast<INA228*>(this)->read_temp_count++; raw = 0; return true; }
};

// Include mocks (order matters)
#include "mocks/pico/stdlib.h"
#include "mocks/pico/multicore.h"
#include "mocks/hardware/timer.h"
#include "mocks/hardware/sync.h"

// Include sampler
#include "sampler.hpp"

void test_decimation() {
    std::cout << "Testing Sampler Decimation..." << std::endl;

    core::SharedContext shared;
    shared.init();
    INA228 ina_mock;

    // Initialize sampler
    device::sampler_init(&shared, &ina_mock);
    device::g_sampler_ctx.timer_active = true;

    // Simulate 20 ticks
    for (int i = 0; i < 20; i++) {
        // ISR increments isr_seq
        device::g_sampler_ctx.isr_seq++;

        // Worker processes it
        device::sampler_do_work(&device::g_sampler_ctx);
    }

    std::cout << "Read Temp Count: " << ina_mock.read_temp_count << std::endl;
    std::cout << "Read VBus Count: " << ina_mock.read_vbus_count << std::endl;

    // Optimized: decimation reads every 10th time
    assert(ina_mock.read_vbus_count == 20);
    assert(ina_mock.read_temp_count == 2);

    std::cout << "Decimation Test: PASS (Optimized)" << std::endl;
}

int main() {
    test_decimation();
    return 0;
}
