#ifndef DEVICE_SAMPLER_HPP
#define DEVICE_SAMPLER_HPP

#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pio_i2c.h"
#include <cstdio>
#include <pico/stdio.h>

#ifdef POWERMONITOR_DEBUG_DMA
#define DEBUG_DMA_PRINT(...)                                                   \
  do {                                                                         \
    printf(__VA_ARGS__);                                                       \
    stdio_flush();                                                             \
  } while (0)
#else
#define DEBUG_DMA_PRINT(...) ((void)0)
#endif
#include <cstdint>

#include "INA228.hpp"
#include "core/raw_sample.hpp"
#include "core/shared_context.hpp"

namespace device {

// Sampler context for Core 1
// Holds pointers to shared resources and local state
struct SamplerContext {
  core::SharedContext *shared; // Pointer to shared context
  INA228 *ina228;              // Pointer to INA228 driver
  repeating_timer_t timer;     // Timer handle
  bool timer_active;           // Is timer currently running

  // ISR -> Worker communication
  volatile uint32_t isr_seq; // Incremented by ISR each tick
  uint32_t worker_seq;       // Last seq processed by worker

  // Duplicate suppression state (Core 1 local)
  bool has_last_sent;
  uint32_t last_vbus_raw;
  int32_t last_vshunt_raw;
  int32_t last_current_raw;
  uint32_t last_sent_time_us;

  // DMA channels
  int dma_tx_chan;
  int dma_rx_chan;

  // PIO details
  PIO pio;
  uint sm;

  // DMA Buffers
  // TX buffer needs to hold instructions for reading all 5 active registers:
  // DIAG_ALRT (2), VBUS (3), VSHUNT (3), CURRENT (3), DIETEMP (2)
  // Roughly ~10 words setup per register + 1 word per rx byte = ~70 words.
  // We'll allocate 128 to be safe.
  uint16_t dma_tx_buf[128];
  uint32_t dma_tx_len;

  // RX buffer holds halfwords from PIO RX FIFO (2+3+3+3+2 = 13 words). We'll
  // allocate 16.
  uint32_t dma_rx_buf[16];
};

// Global sampler context (Core 1 only)
static SamplerContext g_sampler_ctx;

// Timer ISR - extremely short, only increment counter and wake worker
static bool sampler_timer_isr(repeating_timer_t *rt) {
  SamplerContext *ctx = static_cast<SamplerContext *>(rt->user_data);

  // Increment sequence number (atomic on ARM for aligned uint32_t)
  ctx->isr_seq++;

  // Wake up worker via SEV (Send Event)
  __sev();

  return true; // Keep timer repeating
}

// Worker function - does actual I2C reads (called from Core 1 main loop)
static void sampler_do_work(SamplerContext *ctx) {
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

  core::SharedContext *shared = ctx->shared;
  core::RawSample sample;

  // Calculate timestamp relative to stream start
  uint32_t now = time_us_32();
  sample.timestamp_us = now - shared->stream_start_us;
  // Calculate absolute Unix timestamp at sampling time (not send time)
  sample.timestamp_unix_us = static_cast<uint64_t>(now) +
                             static_cast<uint64_t>(shared->epoch_offset_us);

  // Gate reads by conversion-ready (CNVRF) to avoid sampling mid-conversion
  // values from VBUS/VSHUNT/CURRENT registers.
  // However, with DMA, we read DIAG_ALRT first as part of the DMA chain.
  // If CNVRF is not set in the read value, the rest of the read data is
  // technically "old", but we still got it. We should discard and not push
  // to queue if CNVRF isn't set, to avoid duplicate sequence data.

  // Debug: measure timing for the very first iteration to find slowness
  static bool first_iter_timed = false;
  uint32_t t_start = 0, t_dma_start = 0, t_dma_done = 0;
  if (!first_iter_timed) {
    t_start = time_us_32();
  }

  DEBUG_DMA_PRINT("[DMA] reset addrs\n");

  // Guarantee a clean state machine before DMA load:
  // Clears FIFOs, clears errors, and restarts PC at the correct entry point.
  pio_i2c_resume_after_error(ctx->pio, ctx->sm);

  // 1. Reset DMA write/read addresses
  dma_channel_set_write_addr(ctx->dma_rx_chan, ctx->dma_rx_buf, false);
  dma_channel_set_read_addr(ctx->dma_tx_chan, ctx->dma_tx_buf, false);

  // Also reset transfer counts
  dma_channel_set_trans_count(ctx->dma_tx_chan, ctx->dma_tx_len, false);
  // Total RX bytes = 13 data bytes + 15 dummy command bytes (ADDR+W, REG,
  // ADDR+R) = 28 bytes
  dma_channel_set_trans_count(ctx->dma_rx_chan, 28, false);

  DEBUG_DMA_PRINT("[DMA] starting RX+TX (tx_len=%d)\n", (int)ctx->dma_tx_len);

  if (!first_iter_timed)
    t_dma_start = time_us_32();

  // 2. Start RX then TX
  dma_channel_start(ctx->dma_rx_chan);
  dma_channel_start(ctx->dma_tx_chan);

  DEBUG_DMA_PRINT("[DMA] waiting for RX...\n");

  // 3. Wait for RX to complete (blocking for now, to ensure safety)
  dma_channel_wait_for_finish_blocking(ctx->dma_rx_chan);

  if (!first_iter_timed) {
    t_dma_done = time_us_32();
    printf("[TIMING] Setup: %lu us, DMA: %lu us\n",
           (unsigned long)(t_dma_start - t_start),
           (unsigned long)(t_dma_done - t_dma_start));
    first_iter_timed = true;
  }

#ifdef POWERMONITOR_DEBUG_DMA
  DEBUG_DMA_PRINT("[DMA] RX done\n");
  DEBUG_DMA_PRINT("[DMA] rx_buf: ");
  for (int i = 0; i < 28; ++i) {
    // 32-bit words, but data is in lowest 8 bits
    printf("%02X ", ctx->dma_rx_buf[i] & 0xFF);
  }
  DEBUG_DMA_PRINT("\n");
#endif
  // Check if any errors occurred on the PIO side (NACK, timeout)
  bool ok = !pio_i2c_check_error(ctx->pio, ctx->sm);
  if (!ok) {
    DEBUG_DMA_PRINT("[DMA] PIO error detected, resuming\n");
    pio_i2c_resume_after_error(ctx->pio, ctx->sm);
  }

  // 4. Parse the RX buffer
  // With Auto-Push enabled, every byte transmitted (ADDR+W, REG, ADDR+R) is
  // also pushed into the RX FIFO. We have 5 register reads, resulting in 3
  // dummy pushed bytes per register (15 total). Total: 28 bytes per DMA reading
  // sequence. Read 0: 0x0B (2 bytes) -> [0]=ADDR+W, [1]=REG, [2]=ADDR+R,
  // [3..4]=DIAG_ALRT Read 1: 0x05 (3 bytes) -> [5]=ADDR+W, [6]=REG, [7]=ADDR+R,
  // [8..10]=VBUS Read 2: 0x04 (3 bytes) -> [11]=ADDR+W, [12]=REG, [13]=ADDR+R,
  // [14..16]=VSHUNT Read 3: 0x07 (3 bytes) -> [17]=ADDR+W, [18]=REG,
  // [19]=ADDR+R, [20..22]=CURRENT Read 4: 0x06 (2 bytes) -> [23]=ADDR+W,
  // [24]=REG, [25]=ADDR+R, [26..27]=DIETEMP

  uint16_t diag_flags = (ctx->dma_rx_buf[3] << 8) | ctx->dma_rx_buf[4];

  // If CNVRF is not set, we polled too early.
  // Return early without pushing to SpscQueue to avoid duplicates.
#ifndef POWERMONITOR_TEST_MODE
  if (!(diag_flags & kDiagCnvrfBit)) {
    shared->dropped_cnvrf_not_ready++;
    return;
  }
#endif

  if (diag_flags == 0xFFFF) { // Basic I2C error pseudo-check (stuck bus)
    ok = false;
  }

  uint32_t vbus_raw = (ctx->dma_rx_buf[8] << 16) | (ctx->dma_rx_buf[9] << 8) |
                      ctx->dma_rx_buf[10];
  int32_t vshunt_raw = (ctx->dma_rx_buf[14] << 16) |
                       (ctx->dma_rx_buf[15] << 8) | ctx->dma_rx_buf[16];
  int32_t current_raw = (ctx->dma_rx_buf[20] << 16) |
                        (ctx->dma_rx_buf[21] << 8) | ctx->dma_rx_buf[22];
  int16_t temp_raw = (ctx->dma_rx_buf[26] << 8) | ctx->dma_rx_buf[27];

  sample.vbus_raw = vbus_raw;
  sample.vshunt_raw = vshunt_raw;
  sample.current_raw = current_raw;
  sample.dietemp_raw = temp_raw;

  // Build flags from DIAG_ALRT register
  // CNVRF is bit 1, MATHOF is bit 9 in DIAG_ALRT
  sample.flags = 0;
  if (diag_flags & kDiagCnvrfBit)
    sample.flags |= core::SampleFlags::kCnvrf;
  if (diag_flags & (1 << 9))
    sample.flags |= core::SampleFlags::kMathOvf;

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

  // Ensure PIO finishes the STOP condition on the bus before we allow the next
  // trigger
  pio_i2c_wait_idle(ctx->pio, ctx->sm);
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
          int32_t period_us =
              -static_cast<int32_t>(g_sampler_ctx.shared->stream_period_us);

          // Start repeating timer (negative period = repeat)
          // ISR only does seq++ and __sev()
          add_repeating_timer_us(period_us, sampler_timer_isr, &g_sampler_ctx,
                                 &g_sampler_ctx.timer);
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
inline void sampler_init(core::SharedContext *shared, INA228 *ina228) {
  g_sampler_ctx.shared = shared;
  g_sampler_ctx.ina228 = ina228;
  g_sampler_ctx.timer_active = false;
  g_sampler_ctx.has_last_sent = false;
  g_sampler_ctx.last_vbus_raw = 0;
  g_sampler_ctx.last_vshunt_raw = 0;
  g_sampler_ctx.last_current_raw = 0;
  g_sampler_ctx.last_sent_time_us = 0;
}

// Initialize DMA channels and prebuild the TX buffer
// Call this from Core 0 before launching Core 1
inline void sampler_init_dma(PIO pio, uint sm, uint8_t i2c_addr) {
  g_sampler_ctx.dma_tx_chan = dma_claim_unused_channel(true);
  g_sampler_ctx.dma_rx_chan = dma_claim_unused_channel(true);

  // Build the static command sequence to read 5 registers:
  // 0x0B (DIAG_ALRT: 2 bytes), 0x05 (VBUS: 3 bytes), 0x04 (VSHUNT: 3 bytes)
  // 0x07 (CURRENT: 3 bytes), 0x06 (DIETEMP: 2 bytes)
  // Total RX bytes = 13 (so 13 words in RX FIFO)
  size_t len = 0;
  len += pio_i2c_build_command_sequence(&g_sampler_ctx.dma_tx_buf[len],
                                        i2c_addr, 0x0B, 2);
  len += pio_i2c_build_command_sequence(&g_sampler_ctx.dma_tx_buf[len],
                                        i2c_addr, 0x05, 3);
  len += pio_i2c_build_command_sequence(&g_sampler_ctx.dma_tx_buf[len],
                                        i2c_addr, 0x04, 3);
  len += pio_i2c_build_command_sequence(&g_sampler_ctx.dma_tx_buf[len],
                                        i2c_addr, 0x07, 3);
  len += pio_i2c_build_command_sequence(&g_sampler_ctx.dma_tx_buf[len],
                                        i2c_addr, 0x06, 2);
  g_sampler_ctx.dma_tx_len = len;

  // Configure TX DMA (Memory to PIO TX FIFO)
  dma_channel_config c_tx =
      dma_channel_get_default_config(g_sampler_ctx.dma_tx_chan);
  channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_16);
  channel_config_set_read_increment(&c_tx, true);
  channel_config_set_write_increment(&c_tx, false);
  channel_config_set_dreq(&c_tx, pio_get_dreq(pio, sm, true));

  dma_channel_configure(g_sampler_ctx.dma_tx_chan, &c_tx, &pio->txf[sm],
                        g_sampler_ctx.dma_tx_buf, // Source
                        g_sampler_ctx.dma_tx_len, // Length in 16-bit words
                        false);                   // Don't start yet

  // Configure RX DMA (PIO RX FIFO to Memory)
  dma_channel_config c_rx =
      dma_channel_get_default_config(g_sampler_ctx.dma_rx_chan);
  // PIO RX FIFO is read as 32-bit (words) where the byte is at the bottom.
  channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_32);
  channel_config_set_read_increment(&c_rx, false);
  channel_config_set_write_increment(&c_rx, true);
  channel_config_set_dreq(&c_rx, pio_get_dreq(pio, sm, false));

  // Total RX bytes = 13 (data) + 15 (ADDR+W, REG, ADDR+R echoes) = 28 words
  dma_channel_configure(g_sampler_ctx.dma_rx_chan, &c_rx,
                        g_sampler_ctx.dma_rx_buf, // Dest
                        &pio->rxf[sm],
                        28,     // 28 bytes to read
                        false); // Don't start yet

  // Enable RX FIFO auto-push for the I2C PIO SM so reads enter the FIFO
  pio_i2c_rx_enable(pio, sm, true);
}

// Launch Core 1 (call from Core 0 after sampler_init)
inline void sampler_launch_core1() { multicore_launch_core1(core1_entry); }

// Send start command to Core 1 (call from Core 0)
inline void sampler_start() {
  multicore_fifo_push_blocking(
      static_cast<uint32_t>(core::FifoCmd::kStartStream));
}

// Send stop command to Core 1 (call from Core 0)
inline void sampler_stop() {
  multicore_fifo_push_blocking(
      static_cast<uint32_t>(core::FifoCmd::kStopStream));
}

} // namespace device

#endif // DEVICE_SAMPLER_HPP
