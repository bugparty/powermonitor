#include <cstdio>
#include <cassert>
#include <cstring>

// 1. Define guard to prevent including real INA228.hpp
#define INA228_HPP

// 2. Include our mock INA228
#include "mocks/mock_ina228.hpp"

// 3. Include sampler.hpp
// This will include "INA228.hpp" which is guarded, so it sees our class INA228 from the mock
#include "sampler.hpp"

int main() {
    printf("Running test_sampler_decimation...\n");

    // Setup SharedContext
    core::SharedContext shared_ctx;
    shared_ctx.init();

    // Setup INA228 Mock
    INA228 ina228(nullptr, 0x40, 0.015f);

    // Initialize Sampler
    device::sampler_init(&shared_ctx, &ina228);

    // Manually activate sampler state (simulate kStartStream)
    device::g_sampler_ctx.timer_active = true;
    device::g_sampler_ctx.isr_seq = 0;
    device::g_sampler_ctx.worker_seq = 0;

    // Run loop
    const int kNumSamples = 20; // Run enough to trigger decimation twice
    for (int i = 0; i < kNumSamples; ++i) {
        // Simulate ISR tick
        device::g_sampler_ctx.isr_seq++;

        // Run worker
        device::sampler_do_work(&device::g_sampler_ctx);
    }

    printf("Samples Produced: %d\n", shared_ctx.samples_produced);
    printf("Read Temp Count: %d\n", ina228.read_temp_count);
    printf("Read VBUS Count: %d\n", ina228.read_vbus_count);

    if (shared_ctx.samples_produced != kNumSamples) {
        printf("FAILED: Expected %d samples, produced %d\n", kNumSamples, shared_ctx.samples_produced);
        return 1;
    }

    // Logic to verify behavior
    // We want to asserting that currently it IS reading every time.
    // Later we will change assertion.

    bool optimized = (ina228.read_temp_count <= (kNumSamples / 10) + 1);

    if (optimized) {
        printf("STATUS: OPTIMIZED (Decimation Active)\n");
    } else {
        printf("STATUS: BASELINE (No Decimation)\n");
    }

    // Expect optimization: 2 reads for 20 samples (0 and 10)
    int expected_reads = (kNumSamples + 9) / 10; // Simple integer ceil logic for step 10, start 0
    // Actually: 0, 10, 20...
    // indices 0..19. 0%10==0, 10%10==0. So 2 reads.

    if (ina228.read_temp_count == expected_reads) {
        printf("ASSERT: Optimization confirmed (%d reads).\n", ina228.read_temp_count);
        return 0;
    } else {
        printf("ASSERT: Unexpected behavior. Expected %d reads, got %d.\n", expected_reads, ina228.read_temp_count);
        return 1;
    }
}
