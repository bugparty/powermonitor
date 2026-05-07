#pragma once

/**
 * @file crc16_ccitt_false.h
 * @brief PC-side CRC-16 CCITT-FALSE implementation for desktop systems.
 *
 * == WHY THIS FILE EXISTS (Duplicate Protocol Implementations) ==
 *
 * This file is part of the PC-SIDE protocol implementation, optimized for
 * desktop flexibility and performance. A separate device-side implementation
 * exists in device/protocol/crc16.hpp with different trade-offs.
 *
 * == PC-Side Design (this file) ==
 *
 * Target: Desktop Linux/Windows (multi-GHz CPU, GBs of RAM)
 *
 * Design choices driven by desktop environment:
 * - Table-based CRC calculation with 256-entry lookup table (512 bytes)
 *   Reason: ~8x faster than bit-by-bit calculation. On desktop systems,
 *   512 bytes of .rodata is negligible, and the speed improvement matters
 *   when processing high-throughput data streams (1kHz+ samples).
 * - Separate compilation (.h declaration + .cpp definition)
 *   Reason: Standard C++ practice for non-trivial implementations. The
 *   lookup table lives in .cpp to avoid ODR violations and reduce
 *   compilation dependencies.
 * - Incremental CRC support via initial_crc parameter
 *   Reason: Allows computing CRC over non-contiguous data without
 *   allocating a temporary buffer. Useful for header+payload CRC.
 *
 * == Device-Side Implementation (device/protocol/crc16.hpp) ==
 *
 * Target: RP2040 microcontroller (Cortex-M0+, 264KB RAM)
 *
 * Uses bit-by-bit calculation (no lookup table) to save 512 bytes of flash.
 * On a 133MHz MCU, the slower algorithm is still fast enough for our
 * 1MHz max UART baud rate. The header-only design avoids separate
 * compilation overhead and enables better inlining.
 *
 * == When to Modify ==
 *
 * - If processing very high data rates (10kHz+), this table-based
 *   implementation is already optimal.
 * - For embedded targets with hardware CRC support, consider using
 *   the hardware accelerator instead of this software implementation.
 */

#include <cstdint>
#include <cstddef>

namespace protocol {

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len, uint16_t initial_crc = 0xFFFF);

} // namespace protocol
