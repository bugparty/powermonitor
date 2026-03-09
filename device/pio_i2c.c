/**
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Adapted from pico-examples/pio/i2c for powermonitor.
 * Key change vs original: write_blocking has a `nostop` parameter so we can
 * issue a repeated-start when reading registers.
 */
#include "pio_i2c.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

// Bit-field constants matching the TX encoding in i2c.pio
static const int PIO_I2C_ICOUNT_LSB = 10;
static const int PIO_I2C_FINAL_LSB  = 9;
static const int PIO_I2C_DATA_LSB   = 1;
static const int PIO_I2C_NAK_LSB    = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool pio_i2c_check_error(PIO pio, uint sm) {
    return pio_interrupt_get(pio, sm);
}

void pio_i2c_resume_after_error(PIO pio, uint sm) {
    // A stalled SM (e.g., stuck on 'wait 1 pin') will ignore pio_sm_exec.
    // We must hard-reset it to recover safely.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_interrupt_clear(pio, sm);
    
    // Reset SM PC to the bottom wrap target (entry_point)
    // We CANNOT use _wait_blocking here while SM is disabled!
    uint32_t wrap_bottom = (pio->sm[sm].execctrl & PIO_SM0_EXECCTRL_WRAP_BOTTOM_BITS) 
                           >> PIO_SM0_EXECCTRL_WRAP_BOTTOM_LSB;
    pio_sm_exec(pio, sm, pio_encode_jmp(wrap_bottom));
    
    pio_sm_set_enabled(pio, sm, true);
}

static void pio_i2c_rx_enable(PIO pio, uint sm, bool en) {
    if (en)
        hw_set_bits(&pio->sm[sm].shiftctrl, PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS);
    else
        hw_clear_bits(&pio->sm[sm].shiftctrl, PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS);
}

// Write a 16-bit halfword into the TX FIFO (busy-waits for space with timeout)
static bool pio_i2c_put16(PIO pio, uint sm, uint16_t data) {
    uint32_t deadline = time_us_32() + 10000;
    while (pio_sm_is_tx_fifo_full(pio, sm)) {
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            return false;
        }
    }
    *(io_rw_16 *)&pio->txf[sm] = data;
    return true;
}

// Push data only if no error has occurred yet, with timeout
void pio_i2c_put_or_err(PIO pio, uint sm, uint16_t data) {
    uint32_t deadline = time_us_32() + 10000;
    while (pio_sm_is_tx_fifo_full(pio, sm)) {
        if (pio_i2c_check_error(pio, sm)) return;
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            return;
        }
    }
    if (pio_i2c_check_error(pio, sm)) return;
    *(io_rw_16 *)&pio->txf[sm] = data;
}

uint8_t pio_i2c_get(PIO pio, uint sm) {
    uint32_t deadline = time_us_32() + 10000;
    while (pio_sm_is_rx_fifo_empty(pio, sm)) {
        if (pio_i2c_check_error(pio, sm)) return 0;
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            return 0;
        }
    }
    return (uint8_t)pio_sm_get(pio, sm);
}

// Wait until the SM finishes processing all queued data.
// Strategy: wait for TX FIFO to empty, then a brief delay for the SM
// to finish executing the last pulled instruction and stall.
// We deliberately avoid TXSTALL — it has a race condition where clearing
// the flag after the SM has already stalled causes an unrecoverable wait.
static void pio_i2c_wait_idle(PIO pio, uint sm) {
    uint32_t deadline = time_us_32() + 10000;  // 10 ms timeout

    // Wait for TX FIFO to drain (SM is consuming entries)
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
        if (pio_i2c_check_error(pio, sm)) return;
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            return;
        }
    }

    // SM has pulled the last word but may still be executing it.
    // At 1 MHz I2C clock (PIO clkdiv ~3.9), the longest single PIO
    // instruction is ~8 cycles × 0.03us = ~0.25us. A STOP sequence
    // (3 instructions) finishes in < 1us. 2us is very generous.
    busy_wait_us_32(2);
}

// ---------------------------------------------------------------------------
// Sequences: START, REPSTART, STOP
// ---------------------------------------------------------------------------

void pio_i2c_start(PIO pio, uint sm) {
    pio_i2c_put_or_err(pio, sm, 2u << PIO_I2C_ICOUNT_LSB);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC1_SD0]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC0_SD0]);
    pio_i2c_put_or_err(pio, sm, pio_encode_mov(pio_isr, pio_null));
}

void pio_i2c_stop(PIO pio, uint sm) {
    pio_i2c_put_or_err(pio, sm, 2u << PIO_I2C_ICOUNT_LSB);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC0_SD0]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC1_SD0]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC1_SD1]);
}

void pio_i2c_repstart(PIO pio, uint sm) {
    pio_i2c_put_or_err(pio, sm, 4u << PIO_I2C_ICOUNT_LSB);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC0_SD1]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC1_SD1]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC1_SD0]);
    pio_i2c_put_or_err(pio, sm, set_scl_sda_program_instructions[I2C_SC0_SD0]);
    pio_i2c_put_or_err(pio, sm, pio_encode_mov(pio_isr, pio_null));
}

// ---------------------------------------------------------------------------
// Transaction-level API
// ---------------------------------------------------------------------------

int pio_i2c_write_blocking(PIO pio, uint sm, uint8_t addr,
                           uint8_t *txbuf, uint len, bool nostop) {
    int err = 0;
    pio_i2c_start(pio, sm);
    pio_i2c_rx_enable(pio, sm, false);
    // Address + W bit
    pio_i2c_put16(pio, sm, (addr << 2) | 1u);
    uint32_t deadline = time_us_32() + 10000;
    while (len && !pio_i2c_check_error(pio, sm)) {
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            break;
        }
        if (!pio_sm_is_tx_fifo_full(pio, sm)) {
            --len;
            bool is_last = (len == 0);
            // Set Final bit only when not doing a repeated start (nostop=false)
            pio_i2c_put_or_err(pio, sm,
                (*txbuf++ << PIO_I2C_DATA_LSB)
                | ((is_last && !nostop) << PIO_I2C_FINAL_LSB)
                | 1u);
        }
    }
    if (!nostop) {
        pio_i2c_stop(pio, sm);
    } else {
        // Issue repeated-start: SDA high, SCL high, then SDA low while SCL high
        pio_i2c_repstart(pio, sm);
    }
    pio_i2c_wait_idle(pio, sm);
    if (pio_i2c_check_error(pio, sm)) {
        err = -1;
        pio_i2c_resume_after_error(pio, sm);
        pio_i2c_stop(pio, sm);
        pio_i2c_wait_idle(pio, sm);
    }
    return err;
}

// Internal: read after START/REPSTART has already been issued.
// Does NOT issue another START. Goes straight to address + R + data.
static int pio_i2c_raw_read(PIO pio, uint sm, uint8_t addr,
                            uint8_t *rxbuf, uint len) {
    int err = 0;
    pio_i2c_rx_enable(pio, sm, true);

    // Drain stale RX data
    while (!pio_sm_is_rx_fifo_empty(pio, sm))
        (void)pio_i2c_get(pio, sm);

    // Address + R bit
    pio_i2c_put16(pio, sm, (addr << 2) | 3u);

    uint32_t tx_remain = len;
    bool first = true;  // First RX byte is the address echo — discard it

    uint32_t deadline = time_us_32() + 10000;
    while ((tx_remain || len) && !pio_i2c_check_error(pio, sm)) {
        if ((int32_t)(time_us_32() - deadline) > 0) {
            pio->irq_force = 1u << sm;
            break;
        }
        if (tx_remain && !pio_sm_is_tx_fifo_full(pio, sm)) {
            --tx_remain;
            // Last byte: set FINAL and NAK
            pio_i2c_put16(pio, sm,
                (0xffu << PIO_I2C_DATA_LSB)
                | (tx_remain == 0
                    ? (1u << PIO_I2C_FINAL_LSB) | (1u << PIO_I2C_NAK_LSB)
                    : 0u));
        }
        if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            if (first) {
                (void)pio_i2c_get(pio, sm);  // discard address echo
                first = false;
            } else {
                --len;
                *rxbuf++ = pio_i2c_get(pio, sm);
            }
        }
    }

    pio_i2c_stop(pio, sm);
    pio_i2c_wait_idle(pio, sm);
    if (pio_i2c_check_error(pio, sm)) {
        err = -1;
        pio_i2c_resume_after_error(pio, sm);
        pio_i2c_stop(pio, sm);
        pio_i2c_wait_idle(pio, sm);
    }
    return err;
}

int pio_i2c_read_blocking(PIO pio, uint sm, uint8_t addr,
                          uint8_t *rxbuf, uint len) {
    pio_i2c_start(pio, sm);
    return pio_i2c_raw_read(pio, sm, addr, rxbuf, len);
}

// ---------------------------------------------------------------------------
// Convenience: write register pointer (repstart), then read N bytes.
// ---------------------------------------------------------------------------

bool pio_i2c_read_reg(PIO pio, uint sm, uint8_t dev_addr,
                      uint8_t reg, uint8_t *buf, size_t len) {
    // Use STOP + START instead of Repeated-START for maximum robustness.
    // INA228 supports both modes for register reads.
    if (pio_i2c_write_blocking(pio, sm, dev_addr, &reg, 1, /*nostop=*/false) != 0)
        return false;
    return pio_i2c_read_blocking(pio, sm, dev_addr, buf, (uint)len) == 0;
}


