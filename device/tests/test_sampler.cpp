#include <iostream>
#include <cassert>

// Mocks
#include "mocks/pico/stdlib.h"
#include "INA228.hpp"

// Define shared context
#include "core/shared_context.hpp"

// Include code under test
#include "sampler.hpp"

int main() {
    std::cout << "Testing Sampler Decimation..." << std::endl;

    // Setup
    core::SharedContext shared;
    shared.init();
    shared.stream_start_us = 0;

    INA228 sensor(nullptr, 0x40, 0.015f);

    device::sampler_init(&shared, &sensor);

    // Ensure we start fresh
    sensor.read_temp_count = 0;
    sensor.read_vbus_count = 0;

    // Simulate 10 ticks
    for (int i = 0; i < 10; i++) {
        // Update ISR sequence to trigger work
        device::g_sampler_ctx.isr_seq++;

        // Run worker
        device::sampler_do_work(&device::g_sampler_ctx);
    }

    std::cout << "read_temp_count: " << sensor.read_temp_count << std::endl;
    std::cout << "read_vbus_count: " << sensor.read_vbus_count << std::endl;

    // Expect vbus to be read every time
    if (sensor.read_vbus_count != 10) {
        std::cerr << "Expected read_vbus_count to be 10, got " << sensor.read_vbus_count << std::endl;
        return 1;
    }

    // Expect temp to be decimated (1 call per 10 samples)
    // Note: Baseline will fail here (it will be 10)
    if (sensor.read_temp_count != 1) {
        std::cerr << "Expected read_temp_count to be 1, got " << sensor.read_temp_count << std::endl;
        return 1;
    }

    std::cout << "Sampler Decimation: PASS" << std::endl;
    return 0;
}
