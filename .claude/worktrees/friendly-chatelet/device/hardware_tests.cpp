// Hardware self-tests and benchmarks (pack, INA228, PIO I2C).
// Only active when POWERMONITOR_TEST_MODE is defined.

#include "hardware_tests.hpp"
#include "core/raw_sample.hpp"
#include "state_machine.hpp"
#include "core/shared_context.hpp"
#include "sampler.hpp"
#include "INA228.hpp"
#include "pio_i2c.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// Match powermonitor.cpp I2C config
#define I2C_PORT i2c1
#define I2C_SDA 2
#define I2C_SCL 3
#define INA228_ADDR 0x40

#ifdef POWERMONITOR_TEST_MODE

namespace {

using namespace device;
using namespace core;

static uint64_t g_last_test_run_us = 0;

static void run_pio_benchmark(int num_reads, SamplerMode mode, const char* label,
                              DeviceContext& ctx, SharedContext& shared_ctx) {
  printf("\n--- Benchmark: %s (@1MHz) ---\n", label);

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
    const char* name;
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

  g_sampler_ctx.mode = mode;
  g_sampler_ctx.pio = pio;
  g_sampler_ctx.sm = pio_sm;
  if (mode == SamplerMode::kPioDma) {
    DEBUG_DMA_PRINT("[DBG] DMA init start\n");
    sampler_init_dma(pio, pio_sm, INA228_ADDR);
    DEBUG_DMA_PRINT("[DBG] DMA init done, tx_len=%d\n",
                    (int)g_sampler_ctx.dma_tx_len);
  }

  bool pio_ok = true;
  uint64_t pio_start = time_us_64();
  if (pio_ok) {
    uint32_t simulated_seq = 1;
    for (int i = 0; i < num_reads; ++i) {
      DEBUG_DMA_PRINT("[DBG] DMA iter %d start\n", i);
      g_sampler_ctx.isr_seq = simulated_seq++;
      sampler_do_work(&g_sampler_ctx);
      DEBUG_DMA_PRINT("[DBG] DMA iter %d done\n", i);
    }
    uint64_t pio_total = time_us_64() - pio_start;
    DEBUG_DMA_PRINT("[DBG] DMA loop done total=%llu us\n", pio_total);

    RawSample last_dma_sample;
    bool has_sample = false;
    while (!shared_ctx.sample_queue.empty()) {
      has_sample = shared_ctx.sample_queue.pop(last_dma_sample);
    }

    uint32_t slow_power = 0;
    uint64_t slow_energy = 0;
    int64_t slow_charge = 0;
    bool slow_ok = false;
    if (mode == SamplerMode::kPioDma) {
      slow_ok = sampler_read_slow_registers_dma(&g_sampler_ctx, &slow_power,
                                               &slow_energy, &slow_charge);
    } else {
      slow_ok = sampler_read_slow_registers_nodma(&g_sampler_ctx, &slow_power,
                                                  &slow_energy, &slow_charge);
    }

    if (mode == SamplerMode::kPioDma) {
      dma_channel_abort(g_sampler_ctx.dma_rx_chan);
      dma_channel_abort(g_sampler_ctx.dma_tx_chan);
      dma_channel_unclaim(g_sampler_ctx.dma_rx_chan);
      dma_channel_unclaim(g_sampler_ctx.dma_tx_chan);
    }

    pio_sm_set_enabled(pio, pio_sm, false);
    pio_remove_program(pio, &i2c_program, offset);
    pio_sm_unclaim(pio, pio_sm);
    gpio_set_oeover(I2C_SDA, GPIO_OVERRIDE_NORMAL);
    gpio_set_oeover(I2C_SCL, GPIO_OVERRIDE_NORMAL);

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
      printf("[%s] reads=%d  total=%llu us  avg=%.2f us\n", label, num_reads,
             pio_total, (float)pio_total / num_reads);

      float dma_vbus_V = detail::varint2float(last_dma_sample.vbus_raw, 4, 20,
                                              INA228::VBUS_LSB);

      float dma_vshunt_mV =
          detail::varint2float(last_dma_sample.vshunt_raw, 4, 20,
                               ctx.adcrange == 1 ? INA228::VSHUNT_LSB_40MV
                                                  : INA228::VSHUNT_LSB_163MV) *
          1000.0f;

      float dma_current_A = detail::varint2float(
          last_dma_sample.current_raw, 4, 20,
          static_cast<float>(ctx.current_lsb_nA) / 1e9f);
      float dma_dietemp_C =
          static_cast<float>(last_dma_sample.dietemp_raw) * INA228::DIETEMP_LSB;

      float dma_power_W = 0.0f;
      float dma_energy_J = 0.0f;
      float dma_charge_C = 0.0f;

      if (slow_ok) {
        float current_lsb = static_cast<float>(ctx.current_lsb_nA) / 1e9f;
        dma_power_W = INA228::POWER_COEFF * slow_power * current_lsb;
        dma_energy_J = slow_energy * INA228::ENERGY_COEFF * current_lsb;
        dma_charge_C = static_cast<float>(slow_charge) * current_lsb;
      }

      printf("[PIO 0x03] TEMPCO : 0x0000 (Omitted)\n");
      printf("[PIO 0x04] VSHUNT : %.6f mV\n", dma_vshunt_mV);
      printf("[PIO 0x05] VBUS   : %.6f V\n", dma_vbus_V);
      printf("[PIO 0x06] TEMP   : %.2f C\n", dma_dietemp_C);
      printf("[PIO 0x07] CURRENT: %.6f A\n", dma_current_A);

      if (slow_ok) {
        printf("[PIO 0x08] POWER  : %.6f W\n", dma_power_W);
        printf("[PIO 0x09] ENERGY : %.6f J\n", dma_energy_J);
        printf("[PIO 0x0A] CHARGE : %.6f C\n", dma_charge_C);
      } else {
        printf("[PIO 0x08-0x0A] (Slow Read Failed)\n");
      }
    } else {
      printf("[%s] FAILED during benchmark\n", label);
    }
    printf("-----------------------------------------------\n");
    stdio_flush();
  }
}

static void run_pack_tests() {
  printf("\n--- Pack function correctness & timing ---\n");

  const uint32_t u20_vals[] = {0, 1, 0x12345, 0xFFFFF, 0x80000};
  const int32_t s20_vals[] = {0, 1, -1, 524287, -524288, 0x12345, -0x12345};
  const uint32_t u24_vals[] = {0, 1, 0x123456, 0xFFFFFF};
  const uint64_t u40_vals[] = {0, 1, 0x123456789AULL, 0xFFFFFFFFFFULL};
  const int64_t s40_vals[] = {0, 1, -1, 0x7FFFFFFFFFLL, -0x8000000000LL};

  bool all_ok = true;

  {
    bool ok = true;
    for (uint32_t v : u20_vals) {
      uint8_t buf_orig[3], buf_bswap[3];
      core::pack_u20(buf_orig, v);
      core::pack_u20_bswap(buf_bswap, v);
      if (std::memcmp(buf_orig, buf_bswap, 3) != 0) {
        printf("pack_u20 FAIL: v=0x%05lX orig=%02X%02X%02X bswap=%02X%02X%02X\n",
               (unsigned long)v, buf_orig[0], buf_orig[1], buf_orig[2],
               buf_bswap[0], buf_bswap[1], buf_bswap[2]);
        ok = false;
      }
    }
    printf("pack_u20: %s\n", ok ? "OK" : "FAIL");
    all_ok = all_ok && ok;
  }

  {
    bool ok = true;
    for (int32_t v : s20_vals) {
      uint8_t buf_orig[3], buf_bswap[3];
      core::pack_s20(buf_orig, v);
      core::pack_s20_bswap(buf_bswap, v);
      if (std::memcmp(buf_orig, buf_bswap, 3) != 0) {
        printf("pack_s20 FAIL: v=%ld orig=%02X%02X%02X bswap=%02X%02X%02X\n",
               (long)v, buf_orig[0], buf_orig[1], buf_orig[2], buf_bswap[0],
               buf_bswap[1], buf_bswap[2]);
        ok = false;
      }
    }
    printf("pack_s20: %s\n", ok ? "OK" : "FAIL");
    all_ok = all_ok && ok;
  }

  {
    bool ok = true;
    for (uint32_t v : u24_vals) {
      uint8_t buf_orig[3], buf_bswap[3];
      core::pack_u24(buf_orig, v);
      core::pack_u24_bswap(buf_bswap, v);
      if (std::memcmp(buf_orig, buf_bswap, 3) != 0) {
        printf("pack_u24 FAIL: v=0x%06lX\n", (unsigned long)v);
        ok = false;
      }
    }
    printf("pack_u24: %s\n", ok ? "OK" : "FAIL");
    all_ok = all_ok && ok;
  }

  {
    bool ok = true;
    for (uint64_t v : u40_vals) {
      uint8_t buf_orig[5], buf_bswap[5];
      core::pack_u40(buf_orig, v);
      core::pack_u40_bswap(buf_bswap, v);
      if (std::memcmp(buf_orig, buf_bswap, 5) != 0) {
        printf("pack_u40 FAIL: v=0x%010llX\n", (unsigned long long)v);
        ok = false;
      }
    }
    printf("pack_u40: %s\n", ok ? "OK" : "FAIL");
    all_ok = all_ok && ok;
  }

  {
    bool ok = true;
    for (int64_t v : s40_vals) {
      uint8_t buf_orig[5], buf_bswap[5];
      core::pack_s40(buf_orig, v);
      core::pack_s40_bswap(buf_bswap, v);
      if (std::memcmp(buf_orig, buf_bswap, 5) != 0) {
        printf("pack_s40 FAIL: v=%lld\n", (long long)v);
        ok = false;
      }
    }
    printf("pack_s40: %s\n", ok ? "OK" : "FAIL");
    all_ok = all_ok && ok;
  }

  if (!all_ok) {
    printf("Pack correctness: FAILED\n");
    return;
  }
  printf("Pack correctness: all PASS\n");

  constexpr int ITERS = 100000;
  uint64_t t0, t1;
  volatile uint8_t b[5];
  volatile uint32_t sink = 0;

  t0 = time_us_64();
  for (int i = 0; i < ITERS; ++i) {
    core::pack_u20(const_cast<uint8_t*>(b), 0x12345);
    core::pack_s20(const_cast<uint8_t*>(b), 0x12345);
    core::pack_u24(const_cast<uint8_t*>(b), 0x123456);
    core::pack_u40(const_cast<uint8_t*>(b), 0x123456789AULL);
    core::pack_s40(const_cast<uint8_t*>(b), 0x123456789ALL);
    sink ^= b[0] ^ (static_cast<uint32_t>(b[1]) << 8) ^
            (static_cast<uint32_t>(b[2]) << 16);
  }
  t1 = time_us_64();
  uint64_t us_orig = t1 - t0;

  t0 = time_us_64();
  for (int i = 0; i < ITERS; ++i) {
    core::pack_u20_bswap(const_cast<uint8_t*>(b), 0x12345);
    core::pack_s20_bswap(const_cast<uint8_t*>(b), 0x12345);
    core::pack_u24_bswap(const_cast<uint8_t*>(b), 0x123456);
    core::pack_u40_bswap(const_cast<uint8_t*>(b), 0x123456789AULL);
    core::pack_s40_bswap(const_cast<uint8_t*>(b), 0x123456789ALL);
    sink ^= b[0] ^ (static_cast<uint32_t>(b[1]) << 8) ^
            (static_cast<uint32_t>(b[2]) << 16);
  }
  t1 = time_us_64();
  uint64_t us_bswap = t1 - t0;

  printf("Timing (%d iters, orig vs bswap):\n", ITERS);
  printf("  orig:  %llu us total, %.2f us/iter\n", us_orig,
         (float)us_orig / ITERS);
  printf("  bswap: %llu us total, %.2f us/iter\n", us_bswap,
         (float)us_bswap / ITERS);
  printf("  (sink=0x%lX, prevents DCE)\n", (unsigned long)sink);
  printf("-----------------------------------------------\n");
  stdio_flush();
}

}  // namespace

void device::run_hardware_tests(DeviceContext& ctx, core::SharedContext& shared_ctx) {
  uint64_t now = time_us_64();
  if (now - g_last_test_run_us < 1000000ULL) {
    return;
  }
  g_last_test_run_us = now;

  run_pack_tests();

  printf("--- INA228 Hardware Self-Test ---\n");
  if (ctx.ina228) {
    INA228::Measurements meas;

    uint64_t start_time = time_us_64();
    const int NUM_READS = 100;
    bool success = true;
    for (int i = 0; i < NUM_READS; ++i) {
      if (!ctx.ina228->read_all_measurements(meas)) {
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

    run_pio_benchmark(NUM_READS, SamplerMode::kPioNonDma, "PIO I2C (Non-DMA)",
                     ctx, shared_ctx);
    run_pio_benchmark(NUM_READS, SamplerMode::kPioDma, "PIO I2C (DMA)", ctx,
                     shared_ctx);
  } else {
    printf("INA228 context not initialized!\n");
  }
}

#else  // !POWERMONITOR_TEST_MODE

void device::run_hardware_tests(DeviceContext&, core::SharedContext&) {}

#endif  // POWERMONITOR_TEST_MODE
