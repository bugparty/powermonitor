#pragma once

/**
 * @file parser.h
 * @brief PC-side protocol parser for desktop systems.
 *
 * == WHY THIS FILE EXISTS (Duplicate Protocol Implementations) ==
 *
 * This file is part of the PC-SIDE protocol implementation, optimized for
 * desktop flexibility and developer productivity. A separate device-side
 * implementation exists in device/protocol/parser.hpp with different trade-offs.
 *
 * == PC-Side Design (this file) ==
 *
 * Target: Desktop Linux/Windows (multi-GHz CPU, GBs of RAM)
 *
 * Design choices driven by desktop environment:
 * - std::deque<uint8_t> for receive buffer
 *   Reason: Efficient front insertion/removal for byte-by-byte parsing.
 *   Automatically grows to handle partial reads and resync scenarios.
 *   Memory overhead is acceptable on desktop (typically <10KB).
 * - std::function for callback
 *   Reason: Flexibility to use lambdas with captures, member functions,
 *   or function objects. The overhead of type erasure is negligible on
 *   desktop and improves code readability.
 * - std::vector<uint8_t> for DynamicFrame::data
 *   Reason: Dynamically sized payload storage eliminates fixed buffer
 *   size limitations. Simplifies memory management for the caller.
 * - Separate .h/.cpp compilation
 *   Reason: Standard C++ practice. Hides implementation details and
 *   reduces header dependencies.
 * - Configurable max_len parameter
 *   Reason: Allows caller to limit memory usage for untrusted sources.
 *   Default is 1KB, but can be increased for larger payloads.
 *
 * Memory Usage (typical):
 *   - DynamicFrame struct: 24 bytes + vector allocation (payload size)
 *   - Parser object: 64 bytes + deque allocation (typically <1KB)
 *   - Per-frame overhead: negligible on desktop
 *
 * == Device-Side Implementation (device/protocol/parser.hpp) ==
 *
 * Target: RP2040 microcontroller (Cortex-M0+, 264KB RAM)
 *
 * Uses fixed-size stack-allocated arrays for header (6 bytes) and payload
 * (256 bytes + 2 CRC). Total parser object is ~270 bytes with deterministic
 * memory usage. No heap allocation, no std::function overhead. Optimized
 * for constrained embedded environments where every byte counts.
 *
 * == When to Modify ==
 *
 * - If parsing untrusted data from network, consider adding stricter
 *   max_len limits and malformed frame detection.
 * - If processing very high data rates (10kHz+), consider using a ring
 *   buffer instead of deque to reduce memory allocations.
 */

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "protocol/frame_builder.h"

namespace protocol {

class Parser {
public:
    using FrameCallback = std::function<void(const DynamicFrame &, uint64_t receive_time_us)>;

    static constexpr uint16_t kDefaultMaxLen = 1024;

    explicit Parser(FrameCallback callback, uint16_t max_len = kDefaultMaxLen);

    // Set the receive time for the next frame callback
    // This should be called before feed() to ensure accurate timing
    void set_receive_time(uint64_t receive_time_us) { receive_time_us_ = receive_time_us; }

    void feed(const uint8_t *data, size_t len);
    void feed(const std::vector<uint8_t> &data);

    uint64_t crc_fail_count() const { return crc_fail_count_; }
    uint64_t len_fail_count() const { return len_fail_count_; }

    // Test helpers
    //
    // State Machine Design:
    // The parser implements a 4-state FSM that combines the CRC verification and
    // resync logic into the ReadPayload state for simplicity and performance:
    //
    // WAIT_SOF0 -> WAIT_SOF1 -> READ_HEADER -> READ_PAYLOAD -> [WAIT_SOF0]
    //                                          | ^            ^
    //                                          | |            |
    //                                          +- CRC FAIL --+ (auto-resync)
    //
    // Note: While the protocol specification defines 6 conceptual states
    // (WAIT_SOF0, WAIT_SOF1, READ_HEADER, READ_PAYLOAD, VERIFY_CRC, RESYNC),
    // the implementation merges VERIFY_CRC into READ_PAYLOAD and handles RESYNC
    // automatically via buffer search on CRC failures. This design provides:
    // - Zero-overhead state transitions (no dynamic allocation)
    // - Per-instance state isolation (no global state)
    // - Efficient error recovery (single-pass resync)
    //
    enum class State {
        kWaitSof0,      // Waiting for first start byte (0xAA)
        kWaitSof1,      // Waiting for second start byte (0x55)
        kReadHeader,    // Reading 6-byte header (VER, TYPE, FLAGS, SEQ, LEN)
        kReadPayload    // Reading payload + verifying CRC
                        // Note: CRC verification and resync happen here
    };
    State get_state() const { return state_; }
    size_t buffer_size() const { return buffer_.size(); }

private:

    void process();
    void read_header();
    void read_payload();
    void resync();  // Scan for SOF after CRC failure

    FrameCallback callback_;
    std::deque<uint8_t> buffer_;
    State state_ = State::kWaitSof0;
    DynamicFrame current_frame_{};
    uint16_t max_len_ = 1024;
    uint16_t payload_remaining_ = 0;
    uint64_t crc_fail_count_ = 0;
    uint64_t len_fail_count_ = 0;
    uint64_t receive_time_us_ = 0;  // Receive time for current frame
};

} // namespace protocol
