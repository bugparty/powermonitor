// Power Monitor Firmware
// Dual-core operation with real I2C sampling

#include <cstdio>
#include <cmath>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "tusb.h"

#include "protocol/parser.hpp"
#include "state_machine.hpp"
#include "command_handler.hpp"
#include "pico/multicore.h"
#include "sampler.hpp"

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

// Static member definition for CommandHandler::tx_buf_ (keeps it off the stack)
uint8_t device::CommandHandler::tx_buf_[device::CommandHandler::kMaxFrameBytes];

// USB stress mode sample template (fixed values)
static constexpr uint32_t kStressVbusRaw = 0x0F000;     // ~12V equivalent
static constexpr int32_t kStressVshuntRaw = 0x00100;
static constexpr int32_t kStressCurrentRaw = 0x01000;   // Fixed current raw value
static constexpr int16_t kStressDietempRaw = 4480;      // ~35C equivalent
static constexpr uint8_t kStressBurstFramesPerLoop = 1; // Max DATA_SAMPLE sent per main-loop tick

// Shared context for inter-core communication
static core::SharedContext g_shared_ctx;
static bool g_boot_text_sent = false;
static constexpr char kBootTextReport[] = "device online";

// Device-driven time sync: send EVT_TIME_SYNC_REQUEST every 5s when streaming
static constexpr uint64_t kTimeSyncRequestPeriodUs = 5ULL * 1000ULL * 1000ULL;
static constexpr uint64_t kSyncWaitTimeoutUs = 200000ULL;  // 200ms
static uint64_t g_last_time_sync_request_us = 0;

// USB CDC write function
static void usb_cdc_write(const uint8_t* data, size_t len, bool flush = true) {
    if (!data || len == 0 || !tud_cdc_connected()) return;

    // Write in chunks if needed
    size_t remaining = len;
    const uint8_t* ptr = data;

    while (remaining > 0) {
        const uint32_t avail = tud_cdc_write_available();
        if (avail == 0) {
            tud_task();
            if (flush) {
                tud_cdc_write_flush();
            }
            continue;
        }

        const uint32_t to_write = static_cast<uint32_t>(
            (remaining < static_cast<size_t>(avail)) ? remaining : static_cast<size_t>(avail));
        const uint32_t written = tud_cdc_write(ptr, to_write);
        if (written == 0) {
            // Avoid tight spinning when backend temporarily cannot accept bytes.
            tud_task();
            if (flush) {
                tud_cdc_write_flush();
            }
            continue;
        }

        ptr += written;
        remaining -= written;

        // Flushing once at end reduces USB transaction overhead in chunked writes.
        if (flush && remaining == 0) {
            tud_cdc_write_flush();
        }
    }
}

static void usb_cdc_write_flush(const uint8_t* data, size_t len) {
    usb_cdc_write(data, len, true);
}

static void usb_cdc_write_no_flush(const uint8_t* data, size_t len) {
    usb_cdc_write(data, len, false);
}

// Parser callback - called when a complete frame is received
static void on_frame_received(const protocol::Frame& frame, void* user_data) {
    (void)user_data;
    if (g_handler) {
        g_handler->handle_frame(frame);
    }
}

// USB stress mode: emit fixed-value DATA_SAMPLE as fast as main loop allows.
static void process_streaming_usb_stress() {
    if (!g_ctx.is_streaming() || !g_ctx.usb_stress_mode) {
        return;
    }

    for (uint8_t i = 0; i < kStressBurstFramesPerLoop; ++i) {
        core::RawSample sample{};
        sample.timestamp_us = static_cast<uint32_t>(time_us_64() - g_ctx.stream_start_us);
        sample.vbus_raw = kStressVbusRaw;
        sample.vshunt_raw = kStressVshuntRaw;
        sample.current_raw = kStressCurrentRaw;
        sample.dietemp_raw = kStressDietempRaw;
        sample.flags = core::SampleFlags::kCnvrf | core::SampleFlags::kCalValid;
        sample._pad = 0;
        g_handler->send_data_sample(sample);
    }
}

// Pop samples from queue and send
// Core 1 produces samples, Core 0 consumes and sends
static void process_streaming() {
    if (!g_ctx.is_streaming()) return;
    // Pop one samples from queue
    core::RawSample sample;
    if (!g_shared_ctx.sample_queue.pop(sample)) {
        return;
    }else{
        g_handler->send_data_sample(sample);
    }
}

// Streaming processor
static void process_streaming_loop() {
    if (g_ctx.usb_stress_mode) {
        process_streaming_usb_stress();
        return;
    }

    process_streaming();
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
    g_ctx.adcrange = ina228.get_adc_range();
    // INA228 current_lsb = max_current / 2^19; convert A/LSB to nA/LSB for CFG_REPORT.
    g_ctx.current_lsb_nA = static_cast<uint32_t>(
        std::llround((3.5 / 524288.0) * 1e9));
    g_ctx.cal_valid = g_ctx.current_lsb_nA > 0;

    // Initialize shared context and link to device context
    g_shared_ctx.init();
    g_ctx.shared_ctx = &g_shared_ctx;

    // Initialize sampler with shared context and INA228 pointer
    device::sampler_init(&g_shared_ctx, &ina228);

    // Launch Core 1 for sampling
    device::sampler_launch_core1();

    // Initialize command handler
    device::CommandHandler handler(g_ctx, usb_cdc_write_flush, usb_cdc_write_no_flush);
    g_handler = &handler;

    // Initialize parser with callback
    g_parser.set_callback(on_frame_received, nullptr);

    printf("Power Monitor Dual-Core Mode\n");
    printf("Waiting for USB connection...\n");

    // Main loop (Core 0)
    while (true) {
        // TinyUSB housekeeping
        tud_task();

        if (!g_boot_text_sent && g_handler && tud_cdc_connected()) {
            const uint8_t* text = reinterpret_cast<const uint8_t*>(kBootTextReport);
            const size_t text_len = sizeof(kBootTextReport) - 1;
            if (g_handler->send_text_report(text, text_len)) {
                g_boot_text_sent = true;
            }
        }
        // Process incoming USB data (always, for fast T2 capture during sync)
        if (tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            if (count > 0) {
                g_parser.feed(buf, count);
            }
        }

        if (g_ctx.sync_waiting) {
            if (time_us_64() - g_ctx.sync_wait_start_us > kSyncWaitTimeoutUs) {
                g_ctx.sync_waiting = false;
            }
            continue;  // Tight loop: no streaming, no stats
        }

        // Normal path: check 5s timer and maybe send time sync request
        const uint64_t now_us = time_us_64();
        if (g_ctx.is_streaming() && g_handler &&
            (g_last_time_sync_request_us == 0 ||
             now_us - g_last_time_sync_request_us >= kTimeSyncRequestPeriodUs)) {
            if (g_handler->send_time_sync_request()) {
                g_last_time_sync_request_us = now_us;
                g_ctx.sync_waiting = true;
                g_ctx.sync_wait_start_us = now_us;
            }
        }

        if (g_handler) {
            g_handler->maybe_emit_stats_report(now_us);
        }
        tud_task();
        process_streaming_loop();
    }

    return 0;
}
