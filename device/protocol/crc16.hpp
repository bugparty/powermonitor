#ifndef DEVICE_PROTOCOL_CRC16_HPP
#define DEVICE_PROTOCOL_CRC16_HPP

/**
 * @file crc16.hpp
 * @brief Device-side CRC-16 CCITT-FALSE implementation for RP2040 microcontroller.
 *
 * == WHY THIS FILE EXISTS (Duplicate Protocol Implementations) ==
 *
 * This file is part of the DEVICE-SIDE protocol implementation, optimized for
 * embedded constraints. A separate PC-side implementation exists in
 * protocol/crc16_ccitt_false.h with different trade-offs.
 *
 * == Device-Side Constraints (this file) ==
 *
 * Target: RP2040 microcontroller (Cortex-M0+, 264KB RAM, ~2MB flash)
 *
 * Design choices driven by embedded constraints:
 * - Bit-by-bit CRC calculation (no lookup table)
 *   Reason: Saves 512 bytes of ROM/flash at the cost of ~8x slower computation.
 *   On a 133MHz Cortex-M0+, the bit-by-bit loop is still fast enough for
 *   our 1MHz max UART baud rate (CRC time << byte transmission time).
 * - All functions inline (header-only)
 *   Reason: Allows compiler to optimize across translation units without
 *   needing LTO. Also avoids separate compilation overhead for small functions.
 * - No dynamic allocation, no STL dependencies
 *   Reason: Embedded environments often disable exceptions and RTTI. Raw
 *   pointers and fixed-size buffers ensure deterministic memory usage.
 * - Two-buffer CRC function (crc16_ccitt_false_two)
 *   Reason: Computes CRC over header and payload without concatenating them
 *   into a temporary buffer. Avoids stack allocation of up to 4KB on device.
 *
 * == PC-Side Implementation (protocol/crc16_ccitt_false.h) ==
 *
 * Target: Desktop Linux/Windows (multi-GHz CPU, GBs of RAM)
 *
 * Uses 256-entry (512-byte) lookup table for ~8x faster CRC computation.
 * The table is stored in .rodata (read-only memory) and shared across
 * all CRC calls. On desktop systems, 512 bytes is negligible, and the
 * speed improvement is worth it for high-throughput data processing.
 *
 * == When to Modify ==
 *
 * - If moving to a faster MCU with more flash, consider switching to
 *   table-based CRC for better performance.
 * - If CRC becomes a bottleneck (unlikely at current UART speeds),
 *   consider hardware CRC acceleration if available.
 */

#include <cstdint>
#include <cstddef>

namespace protocol {

inline uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000U) {
                crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<uint16_t>(crc << 1U);
            }
        }
    }
    return crc;
}

// Compute CRC over two contiguous buffers without copying into a single buffer.
// Avoids large stack allocations when header and payload are in separate arrays.
inline uint16_t crc16_ccitt_false_two(const uint8_t* a, size_t a_len,
                                      const uint8_t* b, size_t b_len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < a_len; ++i) {
        crc ^= static_cast<uint16_t>(a[i]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U)
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
        }
    }
    for (size_t i = 0; i < b_len; ++i) {
        crc ^= static_cast<uint16_t>(b[i]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U)
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
        }
    }
    return crc;
}

} // namespace protocol

#endif // DEVICE_PROTOCOL_CRC16_HPP
