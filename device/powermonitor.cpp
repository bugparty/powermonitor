// Power Monitor Firmware
// Phase 1: Single-core operation with fake data generation
// Phase 2: Dual-core operation with real I2C sampling

#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "tusb.h"

#include "protocol/parser.hpp"
#include "state_machine.hpp"
#include "command_handler.hpp"

#ifdef PHASE2_DUAL_CORE
#include "pico/multicore.h"
#include "sampler.hpp"
#endif

// I2C Configuration
#define I2C_PORT i2c1
#define I2C_SDA 2
#define I2C_SCL 3
#define INA228_ADDR 0x40
#define INA228_SHUNT_OHMS 0.015f

#include "INA228.hpp"

// Global state
static device::DeviceContext g_ctx;
static protocol::Parser g_parser;
static device::CommandHandler* g_handler = nullptr;

#ifdef PHASE2_DUAL_CORE
// Shared context for inter-core communication
static core::SharedContext g_shared_ctx;
#endif

// USB CDC write function
static void usb_cdc_write(const uint8_t* data, size_t len) {
    if (!tud_cdc_connected()) return;

    // Write in chunks if needed
    size_t remaining = len;
    const uint8_t* ptr = data;

    while (remaining > 0) {
        uint32_t avail = tud_cdc_write_available();
        if (avail == 0) {
            tud_task();
            tud_cdc_write_flush();
            continue;
        }

        uint32_t to_write = (remaining < avail) ? remaining : avail;
        uint32_t written = tud_cdc_write(ptr, to_write);
        ptr += written;
        remaining -= written;

        tud_cdc_write_flush();
    }
}

// Parser callback - called when a complete frame is received
static void on_frame_received(const protocol::Frame& frame, void* user_data) {
    (void)user_data;
    if (g_handler) {
        g_handler->handle_frame(frame);
    }
}

#ifndef PHASE2_DUAL_CORE
// Phase 1: Streaming rate limiter
// In Phase 1, we generate fake data at approximately stream_period_us intervals
static uint64_t g_last_sample_time = 0;

static void process_streaming_phase1() {
    if (!g_ctx.is_streaming()) return;

    uint64_t now = time_us_64();
    uint64_t elapsed = now - g_last_sample_time;

    if (elapsed >= g_ctx.stream_period_us) {
        // Generate and send fake sample
        core::RawSample sample = g_ctx.fake_gen.next();
        sample.timestamp_us = static_cast<uint32_t>(now - g_ctx.stream_start_us);

        g_handler->send_data_sample(sample);
        g_last_sample_time = now;
    }
}
#endif

#ifdef PHASE2_DUAL_CORE
// Phase 2: Pop samples from queue and send
// Core 1 produces samples, Core 0 consumes and sends
static void process_streaming_phase2() {
    if (!g_ctx.is_streaming()) return;

    // Pop all available samples from queue
    core::RawSample sample;
    while (g_shared_ctx.sample_queue.pop(sample)) {
        g_handler->send_data_sample(sample);
    }
}
#endif

// Unified streaming processor
static void process_streaming() {
#ifdef PHASE2_DUAL_CORE
    process_streaming_phase2();
#else
    process_streaming_phase1();
#endif
}

int main() {
    // Initialize stdio (for debug output)
    stdio_init_all();

    // Initialize I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Wait for USB enumeration
    sleep_ms(1000);

    // Initialize INA228
    INA228 ina228(I2C_PORT, INA228_ADDR, INA228_SHUNT_OHMS);
    ina228.configure();
    g_ctx.ina228 = &ina228;

#ifdef PHASE2_DUAL_CORE
    // Phase 2: Initialize shared context and link to device context
    g_shared_ctx.init();
    g_ctx.shared_ctx = &g_shared_ctx;

    // Initialize sampler with shared context and INA228 pointer
    device::sampler_init(&g_shared_ctx, &ina228);

    // Launch Core 1 for sampling
    device::sampler_launch_core1();
#endif

    // Initialize command handler
    device::CommandHandler handler(g_ctx, usb_cdc_write);
    g_handler = &handler;

    // Initialize parser with callback
    g_parser.set_callback(on_frame_received, nullptr);

#ifdef PHASE2_DUAL_CORE
    printf("Power Monitor Phase 2 - Dual-Core Mode\n");
#else
    printf("Power Monitor Phase 1 - Protocol Debug Mode\n");
#endif
    printf("Waiting for USB connection...\n");

    // Main loop (Core 0)
    while (true) {
        // TinyUSB housekeeping
        tud_task();

        // Process incoming USB data
        if (tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            if (count > 0) {
                g_parser.feed(buf, count);
            }
        }

        // Process streaming (Phase 1: generate fake, Phase 2: pop from queue)
        process_streaming();
    }

    return 0;
}
