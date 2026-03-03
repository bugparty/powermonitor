#ifndef DEVICE_SAMPLER_HPP
#define DEVICE_SAMPLER_HPP

#include <cstdint>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/sync.h"

#include "core/shared_context.hpp"
#include "core/raw_sample.hpp"
#include "INA228.hpp"

namespace device {

// Sampler context for Core 1
// Holds pointers to shared resources and local state
struct SamplerContext {
    core::SharedContext* shared;  // Pointer to shared context
    INA228* ina228;               // Pointer to INA228 driver
    repeating_timer_t timer;      // Timer handle
    bool timer_active;            // Is timer currently running

    // ISR -> Worker communication
    volatile uint32_t isr_seq;    // Incremented by ISR each tick
    uint32_t worker_seq;          // Last seq processed by worker

    // Duplicate suppression state (Core 1 local)
    bool has_last_sent;
    uint32_t last_vbus_raw;
    int32_t last_vshunt_raw;
    int32_t last_current_raw;
    uint32_t last_sent_time_us;
};

// Global sampler context (Core 1 only)
static SamplerContext g_sampler_ctx;

// Timer ISR - extremely short, only increment counter and wake worker
static bool sampler_timer_isr(repeating_timer_t* rt) {
    SamplerContext* ctx = static_cast<SamplerContext*>(rt->user_data);

    // Increment sequence number (atomic on ARM for aligned uint32_t)
    ctx->isr_seq++;

    // Wake up worker via SEV (Send Event)
    __sev();

    return true;  // Keep timer repeating
}

// Worker function - does actual I2C reads (called from Core 1 main loop)
static void sampler_do_work(SamplerContext* ctx) {
    constexpr uint16_t kDiagCnvrfBit = (1u << 1);

    // Read current ISR sequence
    uint32_t current_seq = ctx->isr_seq;

    // Nothing new?
    if (current_seq == ctx->worker_seq) {
        return;
    }

    // Calculate how many ticks we missed
    uint32_t missed = current_seq - ctx->worker_seq - 1;
    if (missed > 0) {
        ctx->shared->samples_dropped += missed;
        ctx->shared->dropped_worker_missed_tick += missed;
    }

    // Update worker seq to current (we'll produce one sample for this seq)
    ctx->worker_seq = current_seq;

    core::SharedContext* shared = ctx->shared;
    core::RawSample sample;

    // Calculate timestamp relative to stream start
    uint32_t now = time_us_32();
    sample.timestamp_us = now - shared->stream_start_us;
    // Calculate absolute Unix timestamp at sampling time (not send time)
    sample.timestamp_unix_us = static_cast<uint64_t>(now) + static_cast<uint64_t>(shared->epoch_offset_us);

    // Gate reads by conversion-ready (CNVRF) to avoid sampling mid-conversion
    // values from VBUS/VSHUNT/CURRENT registers.
    bool ok = true;

    uint32_t vbus_raw = 0;
    int32_t vshunt_raw = 0;
    int32_t current_raw = 0;
    int16_t temp_raw = 0;
    uint16_t diag_flags = 0;

    ok &= ctx->ina228->read_diag_alrt(diag_flags);
    if (!ok) {
        sample.flags = core::SampleFlags::kCalValid | core::SampleFlags::kI2cError;
        sample._pad = 0;
        shared->i2c_error = true;
        if (!shared->sample_queue.push(sample)) {
            shared->samples_dropped++;
        } else {
            shared->samples_produced++;
        }
        return;
    }

    ok &= ctx->ina228->read_burst_data(vshunt_raw, vbus_raw, temp_raw, current_raw);

    sample.vbus_raw = vbus_raw;
    sample.vshunt_raw = vshunt_raw;
    sample.current_raw = current_raw;
    sample.dietemp_raw = temp_raw;

    // Build flags from DIAG_ALRT register
    // CNVRF is bit 1, MATHOF is bit 9 in DIAG_ALRT
    sample.flags = 0;
    if (diag_flags & kDiagCnvrfBit) sample.flags |= core::SampleFlags::kCnvrf;
    if (diag_flags & (1 << 9)) sample.flags |= core::SampleFlags::kMathOvf;

    // CAL_VALID: always set since we configure INA228 on startup
    sample.flags |= core::SampleFlags::kCalValid;

    if (!ok) {
        sample.flags |= core::SampleFlags::kI2cError;
        shared->i2c_error = true;
    }

    sample._pad = 0;

    // Push to queue
    if (!shared->sample_queue.push(sample)) {
        // Queue full, sample dropped
        shared->samples_dropped++;
        shared->dropped_queue_full++;
    } else {
        shared->samples_produced++;
        if (ok) {
            ctx->has_last_sent = true;
            ctx->last_vbus_raw = sample.vbus_raw;
            ctx->last_vshunt_raw = sample.vshunt_raw;
            ctx->last_current_raw = sample.current_raw;
            ctx->last_sent_time_us = now;
        }
    }
}

// Core 1 entry point
// Two execution paths:
//   1. Timer ISR: ultra-short, only seq++ and __sev()
//   2. Worker loop: wait for event, do I2C, write to queue
static void core1_entry() {
    // Initialize sampler context
    g_sampler_ctx.timer_active = false;
    g_sampler_ctx.isr_seq = 0;
    g_sampler_ctx.worker_seq = 0;
    g_sampler_ctx.has_last_sent = false;
    g_sampler_ctx.last_vbus_raw = 0;
    g_sampler_ctx.last_vshunt_raw = 0;
    g_sampler_ctx.last_current_raw = 0;
    g_sampler_ctx.last_sent_time_us = 0;

    while (true) {
        // Check for command from Core 0 (non-blocking)
        if (multicore_fifo_rvalid()) {
            uint32_t cmd_raw = multicore_fifo_pop_blocking();

            // Memory barrier to ensure subsequent reads from shared_ctx
            // see the values written by Core 0 before the FIFO push.
            __dmb();

            core::FifoCmd cmd = static_cast<core::FifoCmd>(cmd_raw);

            switch (cmd) {
            case core::FifoCmd::kStartStream:
                if (!g_sampler_ctx.timer_active) {
                    // Reset sequence counters
                    g_sampler_ctx.isr_seq = 0;
                    g_sampler_ctx.worker_seq = 0;
                    g_sampler_ctx.has_last_sent = false;
                    g_sampler_ctx.last_vbus_raw = 0;
                    g_sampler_ctx.last_vshunt_raw = 0;
                    g_sampler_ctx.last_current_raw = 0;
                    g_sampler_ctx.last_sent_time_us = 0;

                    // Reset statistics
                    g_sampler_ctx.shared->reset_stats();

                    // Read period from shared context
                    int32_t period_us = -static_cast<int32_t>(
                        g_sampler_ctx.shared->stream_period_us);

                    // Start repeating timer (negative period = repeat)
                    // ISR only does seq++ and __sev()
                    add_repeating_timer_us(
                        period_us,
                        sampler_timer_isr,
                        &g_sampler_ctx,
                        &g_sampler_ctx.timer
                    );
                    g_sampler_ctx.timer_active = true;
                }
                break;

            case core::FifoCmd::kStopStream:
                if (g_sampler_ctx.timer_active) {
                    cancel_repeating_timer(&g_sampler_ctx.timer);
                    g_sampler_ctx.timer_active = false;
                }
                break;

            default:
                break;
            }
        }

        // If streaming, do work (I2C reads) when ISR signals new tick
        if (g_sampler_ctx.timer_active) {
            sampler_do_work(&g_sampler_ctx);
        }

        // Wait for event (from ISR's __sev() or FIFO)
        // This is low-power and wakes on any event
        __wfe();
    }
}

// Initialize sampler (call from Core 0 before launching Core 1)
inline void sampler_init(core::SharedContext* shared, INA228* ina228) {
    g_sampler_ctx.shared = shared;
    g_sampler_ctx.ina228 = ina228;
    g_sampler_ctx.timer_active = false;
    g_sampler_ctx.has_last_sent = false;
    g_sampler_ctx.last_vbus_raw = 0;
    g_sampler_ctx.last_vshunt_raw = 0;
    g_sampler_ctx.last_current_raw = 0;
    g_sampler_ctx.last_sent_time_us = 0;
}

// Launch Core 1 (call from Core 0 after sampler_init)
inline void sampler_launch_core1() {
    multicore_launch_core1(core1_entry);
}

// Send start command to Core 1 (call from Core 0)
inline void sampler_start() {
    multicore_fifo_push_blocking(static_cast<uint32_t>(core::FifoCmd::kStartStream));
}

// Send stop command to Core 1 (call from Core 0)
inline void sampler_stop() {
    multicore_fifo_push_blocking(static_cast<uint32_t>(core::FifoCmd::kStopStream));
}

} // namespace device

#endif // DEVICE_SAMPLER_HPP
