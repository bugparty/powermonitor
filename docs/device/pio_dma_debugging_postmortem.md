# PIO DMA I2C Debugging Post-Mortem

This document summarizes the series of hardware-level bugs, race conditions, and logical flaws encountered while implementing a zero-overhead 1MHz PIO DMA I2C polling architecture on the RP2040. Over the course of the implementation, we encountered 5 distinct "hangs" (卡死) or "instant failures" that either produced 10ms timeouts (severely slowing down the 100-read benchmark) or resulted in `0.000` data values.

Here is the chronicle of those issues and their hardware-level explanations and fixes.

---

## 1. The 10ms Race Condition: `pio_i2c_wait_idle`
**Symptom:** The initial DMA implementation successfully read data, but took an agonizing ~10.3ms per iteration instead of the expected ~300us.

**Root Cause:**
*   The DMA RX channel completes the moment the *last data byte* is written to the DMA memory buffer (representing the successful completion of the read portion).
*   However, the PIO state machine is still fetching the final `STOP` instruction from the TX FIFO and executing the P (Stop) condition on the bus. This takes approximately `1.2us`.
*   Directly calling `pio_i2c_wait_idle(pio, sm)` after the RX DMA wait caused the CPU to poll the state machine while it was still executing the STOP.
*   A race condition deep inside the `pio_i2c_wait_idle` C-SDK helper triggered a hardcoded 10,000us (10ms) timeout loop, freezing the core before it could continue.

**Fix:** Removed the blocking CPU wait entirely. Once the RX DMA is finished, the CPU is free to parse the data buffer immediately. The PIO hardware handles the asynchronous execution of the 1.2us STOP condition in the background without CPU supervision.

---

## 2. Hard Fault on Incomplete Structs: `kPioNonDma` Pointer Dereference
**Symptom:** Refactoring the benchmark to accept a `SamplerMode` enum (`kPioNonDma` and `kPioDma`) caused the Pico to instantly hard-fault and disconnect USB immediately before `[DBG] DMA init start`.

**Root Cause:**
*   The `SamplerMode` switch correctly bypassed the `sampler_init_dma` function when `kPioNonDma` was selected.
*   However, `sampler_init_dma` was the *only* place where the local `pio` and `pio_sm` hardware pointers were assigned to the global `g_sampler_ctx` struct.
*   When the 100-iteration loop triggered `sampler_do_work_nodma(ctx)`, it passed uninitialized (NULL/0) hardware pointers to the low-level `pio_i2c_read_reg` functions, triggering a core panic.

**Fix:** Explicitly populated `g_sampler_ctx.pio` and `g_sampler_ctx.sm` in the benchmark harness *before* evaluating the DMA mode switch.

---

## 3. The 4us "Instant Completion": Explicit Channel Disable
**Symptom:** The DMA benchmark completed 100 iterations in an impossibly fast 437us (4.37us per loop) and output `0.000` for all sensors.

**Root Cause:**
*   During a code cleanup, `channel_config_set_enable(..., false)` was explicitly added to the RX and TX DMA configuration structures in `sampler_init_dma`.
*   This altered the DMA control registers (specifically `AL1_CTRL_TRIG`) to permanently disable the channel's `EN` bit.
*   When `dma_channel_start(ctx->dma_tx_chan)` was called later, the hardware completely ignored the trigger because the channel was architecturally disabled.
*   The `dma_channel_wait_for_finish_blocking()` function checks the `BUSY` flag. Since the channel never started, `BUSY` was false, and the function returned instantly.
*   The memory buffer remained filled with zeroes, causing the parse logic to format `0.000`.

**Fix:** Removed the explicit `channel_config_set_enable(..., false)` logic. The Pico SDK's `dma_channel_get_default_config()` implicitly prepares the channel with `EN=1`. The deferred start is correctly handled by passing `false` to the final `trigger` argument of `dma_channel_configure()`.

---

## 4. The Autopush Trap: Stalled RX FIFOs
**Symptom:** In `kPioNonDma` mode, the benchmark printed 8 valid diagnostic registers, then proceeded to take 1 full second (1002226us) for 100 reads, outputting `0.000` for all sensors.

**Root Cause:**
*   An attempt to "guarantee a clean state" added `pio_i2c_rx_enable(ctx->pio, ctx->sm, false)` at the top of the polling loop, which explicitly turned off the RP2040's hardware `Autopush` feature.
*   The Official `pico-examples` `i2c.pio` bitbanger program reads incoming bits using `in pins, 1` into the Input Shift Register (ISR). Crucially, the program *contains no explicit `push` instructions*. It relies 100% on the hardware automatically pushing the ISR into the RX FIFO once 8 bits are collected.
*   With Autopush disabled, the bits filled the ISR and stayed there forever.
*   The CPU called `pio_i2c_read_reg`, which called `pio_i2c_read_blocking`. The CPU waited indefinitely on an empty RX FIFO until it hit the C-SDK's 10ms hard timeout, resulting in a failed transaction (0 data) and a 10ms delay per loop.

**Fix:** Removed the `pio_i2c_rx_enable(..., false)` call. Standard PIO I2C bitbanging strictly requires Autopush to be enabled at all times.

---

## 5. Corrupting the Bus: Aggressive PC Resets
**Symptom:** Even after fixing Autopush, the DMA and Non-DMA benchmarks still hung on the 8th diagnostic register (`0x0A CHARGE`) and failed the 100-loop benchmarks with cascading timeouts.

**Root Cause:**
*   Code was added to forcefully execute `pio_i2c_resume_after_error(ctx->pio, ctx->sm)` before *every single transaction* inside the polling loop.
*   `pio_i2c_resume_after_error` disables the state machine, clears the FIFOs, forcefully modifies the Program Counter (PC) to jump back to `entry_point`, and re-enables the machine.
*   In a high-speed loop, the previous I2C transaction is likely still transitioning its final electrical `STOP` condition (SDA rising while SCL is high) taking a few extra microseconds.
*   Aggressively resetting the PC *during* this transition aborted the STOP condition midway, corrupting the electrical state of the SDA/SCL lines (e.g., leaving SDA unexpectedly pulled low).
*   The INA228 sensor perceived this as an invalid bus state or an un-acknowledged transfer, leading to subsequent NAKs or clock stretching deadlocks (the 10ms timeout).

**Fix:** Eliminated the aggressive `pio_i2c_resume_after_error` loops. The PIO I2C state machine is designed to naturally wrap to `entry_point` after an execution sequence. For DMA preparation, a gentle `pio_sm_clear_fifos()` was used to safely flush any remnant buffer state without touching the running PC or the physical pin drives.
