// Power Monitor Firmware
// Dual-core operation with real I2C sampling

#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <cmath>
#include <cstdio>

#include "command_handler.hpp"
#include "pico/multicore.h"
#include "protocol/parser.hpp"
#include "sampler.hpp"
#include "state_machine.hpp"

// I2C Configuration
#define I2C_PORT i2c1
#define I2C_SDA 2
#define I2C_SCL 3
#define INA228_ADDR 0x40
#define INA228_SHUNT_OHMS 0.015f

#include "INA228.hpp"
#include "hardware/clocks.h"
#include "pio_i2c.h"

// Global state
static device::DeviceContext g_ctx;
static protocol::Parser g_parser;
static device::CommandHandler *g_handler = nullptr;

// Static member definition for CommandHandler::tx_buf_ (keeps it off the stack)
uint8_t device::CommandHandler::tx_buf_[device::CommandHandler::kMaxFrameBytes];

// USB stress mode sample template (fixed values)
static constexpr uint32_t kStressVbusRaw = 0x0F000; // ~12V equivalent
static constexpr int32_t kStressVshuntRaw = 0x00100;
static constexpr int32_t kStressCurrentRaw = 0x01000; // Fixed current raw value
static constexpr int16_t kStressDietempRaw = 4480;    // ~35C equivalent
static constexpr uint8_t kStressBurstFramesPerLoop =
    1; // Max DATA_SAMPLE sent per main-loop tick

// Shared context for inter-core communication
static core::SharedContext g_shared_ctx;
static bool g_boot_text_sent = false;
static constexpr char kBootTextReport[] = "device online";

// Device-driven time sync: send EVT_TIME_SYNC_REQUEST every 2 minutes when
// streaming
static constexpr uint64_t kTimeSyncRequestPeriodUs =
    120ULL * 1000ULL * 1000ULL;                           // 2 minutes
static constexpr uint64_t kSyncWaitTimeoutUs = 200000ULL; // 200ms
static uint64_t g_last_time_sync_request_us = 0;

// T2 timing: capture time when frame is received via USB
namespace device {
uint64_t g_last_frame_recv_time_us = 0;
}

// USB CDC write function
static void usb_cdc_write(const uint8_t *data, size_t len, bool flush = true) {
  if (!data || len == 0 || !tud_cdc_connected())
    return;

  // Write in chunks if needed
  size_t remaining = len;
  const uint8_t *ptr = data;

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
        (remaining < static_cast<size_t>(avail)) ? remaining
                                                 : static_cast<size_t>(avail));
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

static void usb_cdc_write_flush(const uint8_t *data, size_t len) {
  usb_cdc_write(data, len, true);
}

static void usb_cdc_write_no_flush(const uint8_t *data, size_t len) {
  usb_cdc_write(data, len, false);
}

// Parser callback - called when a complete frame is received
static void on_frame_received(const protocol::Frame &frame, void *user_data) {
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
    sample.timestamp_us =
        static_cast<uint32_t>(time_us_64() - g_ctx.stream_start_us);
    sample.vbus_raw = kStressVbusRaw;
    sample.vshunt_raw = kStressVshuntRaw;
    sample.current_raw = kStressCurrentRaw;
    sample.dietemp_raw = kStressDietempRaw;
    sample.flags = core::SampleFlags::kCnvrf | core::SampleFlags::kCalValid;
    sample._pad = 0;
    g_handler->send_data_sample_noflush(sample);
  }
}

// Pop samples from queue and send
// Core 1 produces samples, Core 0 consumes and sends
static void process_streaming() {
  if (!g_ctx.is_streaming())
    return;
  // Pop and send multiple samples per call to keep up with 1kHz rate
  // Process up to 32 samples per iteration
  for (int i = 0; i < 32; i++) {
    core::RawSample sample;
    if (!g_shared_ctx.sample_queue.pop(sample)) {
      break; // Queue empty, done
    }
    g_handler->send_data_sample_noflush(sample);
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

#ifdef POWERMONITOR_TEST_MODE
static uint64_t g_last_test_run_us = 0;

static void run_pio_benchmark(int NUM_READS) {
  printf("\n--- Benchmark: HW I2C vs PIO I2C (@1MHz) ---\n");
  // (We'll print HW I2C stats from the caller before calling this)

  // === PIO I2C Benchmark ===
  i2c_deinit(I2C_PORT);
  gpio_init(I2C_SDA);
  gpio_init(I2C_SCL);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);
  PIO pio = pio0;
  uint pio_sm = pio_claim_unused_sm(pio, true);
  uint offset = pio_add_program(pio, &i2c_program);
  i2c_program_init(pio, pio_sm, offset, I2C_SDA, I2C_SCL);
  static const struct {
    uint8_t reg;
    uint8_t len;
    const char *name;
  } diag_regs[] = {
      {0x03, 2, "TEMPCO "}, {0x04, 3, "VSHUNT "}, {0x05, 3, "VBUS   "},
      {0x06, 2, "DIETEMP"}, {0x07, 3, "CURRENT"}, {0x08, 3, "POWER  "},
      {0x09, 5, "ENERGY "}, {0x0A, 5, "CHARGE "},
  };
  bool diag_ok = true;
  for (int r = 0; r < 8; ++r) {
    printf("[PIO] reading 0x%02X %s (%d bytes)... ", diag_regs[r].reg,
           diag_regs[r].name, diag_regs[r].len);
    uint8_t dbuf[5] = {};
    bool ok = pio_i2c_read_reg(pio, pio_sm, INA228_ADDR, diag_regs[r].reg, dbuf,
                               diag_regs[r].len);
    if (ok) {
      printf("OK (");
      for (int b = 0; b < diag_regs[r].len; ++b)
        printf("%02X", dbuf[b]);
      printf(")\n");
    } else {
      printf("FAIL\n");
      diag_ok = false;
      break;
    }
  }

  DEBUG_DMA_PRINT("[DBG] DMA init start\n");
  device::sampler_init_dma(pio, pio_sm, INA228_ADDR);
  DEBUG_DMA_PRINT("[DBG] DMA init done, tx_len=%d\n",
                  (int)device::g_sampler_ctx.dma_tx_len);

  // We run 100 PIO DMA reads for actual load benchmarking
  bool pio_ok = true;
  uint64_t pio_start = time_us_64();
  if (pio_ok) {
    uint32_t simulated_seq = 1;
    for (int i = 0; i < NUM_READS; ++i) {
      DEBUG_DMA_PRINT("[DBG] DMA iter %d start\n", i);
      // Simulate an ISR timer tick
      device::g_sampler_ctx.isr_seq = simulated_seq++;
      // Run the sampler iteration (starts DMA, blocks until done, pushes to
      // queue)
      device::sampler_do_work(&device::g_sampler_ctx);
      DEBUG_DMA_PRINT("[DBG] DMA iter %d done\n", i);
    }
    uint64_t pio_total = time_us_64() - pio_start;
    DEBUG_DMA_PRINT("[DBG] DMA loop done total=%llu us\n", pio_total);
    // Retrieve the last parsed DMA sample from the queue
    core::RawSample last_dma_sample;
    bool has_sample = false;
    while (!g_shared_ctx.sample_queue.empty()) {
      has_sample = g_shared_ctx.sample_queue.pop(last_dma_sample);
    }

    dma_channel_unclaim(device::g_sampler_ctx.dma_rx_chan);
    dma_channel_unclaim(device::g_sampler_ctx.dma_tx_chan);

    pio_sm_set_enabled(pio, pio_sm, false);
    pio_remove_program(pio, &i2c_program, offset);
    pio_sm_unclaim(pio, pio_sm);
    gpio_set_oeover(I2C_SDA, GPIO_OVERRIDE_NORMAL);
    gpio_set_oeover(I2C_SCL, GPIO_OVERRIDE_NORMAL);

    // I2C Bus Clear: If the PIO aborted mid-transaction, the INA228 might
    // be holding SDA low. This will permanently hang the Pico SDK HW I2C.
    // We bit-bang SCL up to 9 times to clock out any stuck bits.
    gpio_init(I2C_SDA);
    gpio_init(I2C_SCL);
    gpio_set_dir(I2C_SDA, GPIO_IN);
    gpio_set_dir(I2C_SCL, GPIO_OUT);
    gpio_put(I2C_SCL, 1);
    sleep_us(10);
    for (int i = 0; i < 9; ++i) {
      if (gpio_get(I2C_SDA))
        break;
      gpio_put(I2C_SCL, 0);
      sleep_us(5);
      gpio_put(I2C_SCL, 1);
      sleep_us(5);
    }
    gpio_set_dir(I2C_SDA, GPIO_OUT);
    gpio_put(I2C_SDA, 0);
    sleep_us(5);
    gpio_put(I2C_SCL, 1);
    sleep_us(5);
    gpio_put(I2C_SDA, 1);
    sleep_us(5);
    i2c_init(I2C_PORT, 1000 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    if (has_sample) {
      // Print PIO benchmark results
      printf("[PIO DMA] reads=%d  total=%llu us  avg=%.2f us\n", NUM_READS,
             pio_total, (float)pio_total / NUM_READS);

      // Convert raw integers back to floats using INA228 conversion rules
      float dma_vbus_V = detail::varint2float(last_dma_sample.vbus_raw, 4, 20,
                                              INA228::VBUS_LSB);

      float dma_vshunt_mV =
          detail::varint2float(last_dma_sample.vshunt_raw, 4, 20,
                               g_ctx.adcrange == 1 ? INA228::VSHUNT_LSB_40MV
                                                   : INA228::VSHUNT_LSB_163MV) *
          1000.0f;

      float dma_current_A =
          detail::varint2float(last_dma_sample.current_raw, 4, 20,
                               static_cast<float>(g_ctx.current_lsb_nA) / 1e9f);
      float dma_dietemp_C =
          static_cast<float>(last_dma_sample.dietemp_raw) * INA228::DIETEMP_LSB;

      printf("[PIO 0x03] TEMPCO : 0x0000 (Omitted)\n");
      printf("[PIO 0x04] VSHUNT : %.6f mV\n", dma_vshunt_mV);
      printf("[PIO 0x05] VBUS   : %.6f V\n", dma_vbus_V);
      printf("[PIO 0x06] TEMP   : %.2f C\n", dma_dietemp_C);
      printf("[PIO 0x07] CURRENT: %.6f A\n", dma_current_A);
      printf("[PIO 0x08] POWER  : (Omitted)\n");
      printf("[PIO 0x09] ENERGY : (Omitted)\n");
      printf("[PIO 0x0A] CHARGE : (Omitted)\n");
    } else {
      printf("[PIO DMA] FAILED during benchmark\n");
    }
    printf("-----------------------------------------------\n");
    stdio_flush();
  }
}

static void run_hardware_tests() {
  uint64_t now = time_us_64();
  if (now - g_last_test_run_us < 1000000ULL) {
    return; // not yet 1 second
  }
  g_last_test_run_us = now;

  printf("--- INA228 Hardware Self-Test ---\n");
  if (g_ctx.ina228) {
    INA228::Measurements meas;

    // Benchmark 100 consecutive reads
    uint64_t start_time = time_us_64();
    const int NUM_READS = 100;
    bool success = true;
    for (int i = 0; i < NUM_READS; ++i) {
      if (!g_ctx.ina228->read_all_measurements(meas)) {
        success = false;
        break;
      }
    }
    uint64_t end_time = time_us_64();

    if (!success) {
      printf("Failed to read all measurements from INA228 during benchmark!\n");
      return;
    }

    uint64_t total_duration_us = end_time - start_time;
    float avg_duration_us = static_cast<float>(total_duration_us) / NUM_READS;

    printf("[0x03] SHUNT_TEMPCO : 0x%04X\n", meas.shunt_tempco);
    printf("[0x04] VSHUNT       : %.6f mV\n", meas.vshunt_mV);
    printf("[0x05] VBUS         : %.6f V\n", meas.vbus_V);
    printf("[0x06] DIETEMP      : %.2f C\n", meas.dietemp_C);
    printf("[0x07] CURRENT      : %.6f A\n", meas.current_A);
    printf("[0x08] POWER        : %.6f W\n", meas.power_W);
    printf("[0x09] ENERGY       : %.6f J\n", meas.energy_J);
    printf("[0x0A] CHARGE       : %.6f C\n", meas.charge_C);

    printf("\n--- Benchmark: HW I2C vs PIO I2C (@1MHz) ---\n");
    printf("[HW  I2C] reads=%d  total=%llu us  avg=%.2f us\n", NUM_READS,
           total_duration_us, avg_duration_us);

    // Run PIO Benchmark directly
    run_pio_benchmark(NUM_READS);
  } else {
    printf("INA228 context not initialized!\n");
  }
}
#endif

int main() {
  // Initialize stdio (for debug output)
  stdio_init_all();

  // Wait until the host actually opens CDC (more robust, no fixed 2s delay
  // needed)
  absolute_time_t deadline = make_timeout_time_ms(3000);
  while (!tud_cdc_connected() && !time_reached(deadline)) {
    sleep_ms(10);
  }

  // Initialize I2C
  i2c_init(I2C_PORT, 1000 * 1000); // 1 MHz
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);

  // Initialize INA228
  INA228 ina228(I2C_PORT, INA228_ADDR, INA228_SHUNT_OHMS);
  ina228.configure();
  g_ctx.ina228 = &ina228;
  g_ctx.adcrange = ina228.get_adc_range();
  // INA228 current_lsb = max_current / 2^19; convert A/LSB to nA/LSB for
  // CFG_REPORT.
  g_ctx.current_lsb_nA =
      static_cast<uint32_t>(std::llround((3.5 / 524288.0) * 1e9));
  g_ctx.cal_valid = g_ctx.current_lsb_nA > 0;

  // Initialize shared context and link to device context
  g_shared_ctx.init();
  g_ctx.shared_ctx = &g_shared_ctx;

  // Initialize sampler with shared context and INA228 pointer
  device::sampler_init(&g_shared_ctx, &ina228);

#ifndef POWERMONITOR_TEST_MODE
  // Transition from HW I2C to PIO I2C for high-speed sampling
  i2c_deinit(I2C_PORT);
  gpio_init(I2C_SDA);
  gpio_init(I2C_SCL);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);

  PIO pio = pio0;
  uint sm = pio_claim_unused_sm(pio, true);
  uint offset = pio_add_program(pio, &i2c_program);
  i2c_program_init(pio, sm, offset, I2C_SDA, I2C_SCL);

  // Set PIO clock to 1 MHz
  float div = (float)clock_get_hz(clk_sys) / (32 * 1000000);
  pio_sm_set_clkdiv(pio, sm, div);

  // Initialize DMA channels and buffers for the sampler
  device::sampler_init_dma(pio, sm, INA228_ADDR);

  // Launch Core 1 for sampling
  device::sampler_launch_core1();
#endif

  // Initialize command handler
  device::CommandHandler handler(g_ctx, usb_cdc_write_flush,
                                 usb_cdc_write_no_flush);
  g_handler = &handler;

  // Initialize parser with callback
  g_parser.set_callback(on_frame_received, nullptr);

  printf("Power Monitor Dual-Core Mode\n");
  printf("Waiting for USB connection...\n");

  // Main loop (Core 0)
  while (true) {
    // TinyUSB housekeeping
    tud_task();

#ifdef POWERMONITOR_TEST_MODE
    if (tud_cdc_connected()) {
      run_hardware_tests();
    }
    continue;
#endif
    // Send boot text: retry each iteration until successful (USB may not be
    // ready on first attempt)
    if (!g_boot_text_sent && tud_cdc_connected()) {
      const uint8_t *text = reinterpret_cast<const uint8_t *>(kBootTextReport);
      const size_t text_len = sizeof(kBootTextReport) - 1;
      // Keep retrying each loop iteration until send succeeds
      if (g_handler && g_handler->send_text_report(text, text_len)) {
        g_boot_text_sent = true;
      }
    }
    // Process incoming USB data (always, for fast T2 capture during sync)
    if (tud_cdc_available()) {
      uint8_t buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));
      if (count > 0) {
        device::g_last_frame_recv_time_us = time_us_64();
        g_parser.feed(buf, count);
      }
    }

    if (g_ctx.sync_waiting) {
      if (time_us_64() - g_ctx.sync_wait_start_us > kSyncWaitTimeoutUs) {
        g_ctx.sync_waiting = false;
      }
      continue; // Tight loop: no streaming, no stats
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
