#ifndef DEVICE_COMMAND_HANDLER_HPP
#define DEVICE_COMMAND_HANDLER_HPP

#include <cstdint>
#include <cstring>
#include "pico/stdlib.h"

#include "protocol/frame_defs.hpp"
#include "protocol/frame_builder.hpp"
#include "state_machine.hpp"
#include "core/raw_sample.hpp"
// sampler.hpp removed, moved to cpp

namespace device {

constexpr uint16_t kStreamMaskUsbStressMode = 0x8000;

// USB CDC write function type
using UsbWriteFn = void (*)(const uint8_t* data, size_t len);

// Command handler - processes incoming frames and sends responses
class CommandHandler {
public:
    explicit CommandHandler(DeviceContext& ctx, UsbWriteFn write_fn);

    CommandHandler(DeviceContext& ctx, UsbWriteFn write_fn, UsbWriteFn data_write_fn);

    void maybe_emit_stats_report(uint64_t now_us);

    // Handle a received frame (called from parser callback)
    void handle_frame(const protocol::Frame& frame);

    // Build and send a DATA_SAMPLE frame from a RawSample
    // Use template parameter to choose flush behavior at compile time
    template<bool FlushAfterWrite = true>
    size_t send_data_sample_impl(const core::RawSample& sample) {
        // Build DATA_SAMPLE payload (16 bytes)
        protocol::DataSamplePayload payload;
        payload.timestamp_us = sample.timestamp_us;
        payload.flags = sample.flags;

        // Pack 20-bit values
        core::pack_u20(payload.vbus20, sample.vbus_raw);
        core::pack_s20(payload.vshunt20, sample.vshunt_raw);
        core::pack_s20(payload.current20, sample.current_raw);
        payload.dietemp16 = sample.dietemp_raw;

        // Build and send frame
        size_t len = protocol::build_frame(
            tx_buf_, sizeof(tx_buf_),
            protocol::FrameType::kData,
            0,  // flags
            ctx_.next_data_seq(),
            static_cast<uint8_t>(protocol::MsgId::kDataSample),
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(payload)
        );

        if (len > 0) {
            if constexpr (FlushAfterWrite) {
                if (write_flush_fn_) {
                    write_flush_fn_(tx_buf_, len);
                }
            } else {
                if (write_noflush_fn_) {
                    write_noflush_fn_(tx_buf_, len);
                }
            }
            ctx_.samples_sent++;
        }
        return len;
    }

    // Default: flush after write (for real USB device)
    inline size_t send_data_sample(const core::RawSample& sample) {
        return send_data_sample_impl<true>(sample);
    }

    // For simulation: no flush needed
    inline size_t send_data_sample_noflush(const core::RawSample& sample) {
        return send_data_sample_impl<false>(sample);
    }

    // Send asynchronous text event to PC.
    // Payload is raw text bytes with length range [1, 4096].
    bool send_text_report(const uint8_t* text, size_t text_len);

    // Send EVT_TIME_SYNC_REQUEST to notify PC to initiate time sync (no payload).
    bool send_time_sync_request();

private:
    void handle_ping(const protocol::Frame& frame);
    void handle_time_sync(const protocol::Frame& frame);
    void handle_time_adjust(const protocol::Frame& frame);
    void handle_time_set(const protocol::Frame& frame);
    void handle_set_cfg(const protocol::Frame& frame);
    void handle_get_cfg(const protocol::Frame& frame);
    void handle_reg_read(const protocol::Frame& frame);
    void handle_reg_write(const protocol::Frame& frame);
    void handle_stream_start(const protocol::Frame& frame);
    void handle_stream_stop(const protocol::Frame& frame);
    void send_rsp(uint8_t seq, uint8_t orig_msgid, protocol::Status status);
    void send_cfg_report();
    bool send_stats_report(uint16_t window_ms);

    DeviceContext& ctx_;
    UsbWriteFn write_flush_fn_;
    UsbWriteFn write_noflush_fn_;
    // Use kMaxTxPayloadLen for the send buffer (needs to hold TEXT_REPORT up to 4096 bytes).
    // Static to keep it off the stack and avoid RP2040 stack overflow.
    static constexpr size_t kMaxFrameBytes = 2 + protocol::kHeaderSize + protocol::kMaxTxPayloadLen + 2;
    static uint8_t tx_buf_[kMaxFrameBytes];
    uint16_t stats_report_seq_ = 0;
    uint64_t last_stats_report_us_ = 0;
};

} // namespace device

#endif // DEVICE_COMMAND_HANDLER_HPP
