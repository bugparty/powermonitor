#include <cstdio>
#include <cassert>
#include <cstring>

// Mocks
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "pico/multicore.h"

// Implement Mocks
extern "C" {

// I2C Mock
static int i2c_write_count = 0;
static int dietemp_read_count = 0;

int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop) {
    i2c_write_count++;
    if (addr == 0x40 && len == 1 && src[0] == 0x06) { // 0x06 is DIETEMP
        dietemp_read_count++;
    }
    return len;
}

int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop) {
    // Return dummy data
    memset(dst, 0, len);
    return len;
}

int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint64_t timeout_us) {
    return i2c_write_blocking(i2c, addr, src, len, nostop);
}

int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint64_t timeout_us) {
    return i2c_read_blocking(i2c, addr, dst, len, nostop);
}

// Timer Mock
bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out) {
    return true;
}
bool cancel_repeating_timer(repeating_timer_t *timer) {
    return true;
}

// Multicore Mock
void multicore_launch_core1(multicore_entry_t entry) {}
void multicore_fifo_push_blocking(uint32_t data) {}
uint32_t multicore_fifo_pop_blocking() { return 0; }
bool multicore_fifo_rvalid() { return false; }

} // extern "C"

// Include target
#include "sampler.hpp"

// INA228 implementation is linked, so we can use INA228 class.

int main() {
    printf("Starting test_sampler_decimation...\n");

    // Setup Context
    core::SharedContext shared;
    INA228 ina228(nullptr, 0x40, 0.015f); // Use dummy i2c_inst

    // Initialize shared context basics
    shared.init();

    // Initialize sampler context
    device::sampler_init(&shared, &ina228);

    // Run the loop
    for (int i = 0; i < 20; i++) {
        device::g_sampler_ctx.isr_seq++;
        device::sampler_do_work(&device::g_sampler_ctx);
    }

    printf("Dietemp read count: %d\n", dietemp_read_count);

    // Expected: 2 (index 0 and 10) for optimized code.
    // If not optimized, it will be 20.

    if (dietemp_read_count == 2) {
        printf("Test PASSED\n");
        return 0;
    } else {
        printf("Test FAILED: Expected 2, got %d\n", dietemp_read_count);
        return 1;
    }
}
