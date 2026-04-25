#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "protocol/frame_builder.h"
#include "protocol/parser.h"
#include "protocol/unpack.h"
#include "sim/virtual_link.h"

namespace node {

class PCNode {
public:
    explicit PCNode(sim::VirtualLinkEndpoint *endpoint);

    void tick(uint64_t now_us);

    void send_ping(uint64_t now_us);
    void send_time_sync(uint64_t now_us);
    void send_time_adjust(int64_t offset_us, uint64_t now_us);
    void send_time_set(uint64_t unix_time_us, uint64_t now_us);
    void send_set_cfg(uint16_t config_reg, uint16_t adc_config_reg, uint16_t shunt_cal,
                      uint16_t shunt_tempco, uint64_t now_us);
    void send_get_cfg(uint64_t now_us);
    void send_stream_start(uint16_t period_us, uint16_t mask, uint64_t now_us);
    void send_stream_stop(uint64_t now_us);

    uint64_t crc_fail_count() const { return parser_.crc_fail_count(); }
    uint64_t data_drop_count() const { return data_drop_count_; }
    uint64_t timeout_count() const { return timeout_count_; }
    uint64_t retransmit_count() const { return retransmit_count_; }
    uint64_t get_rx_count(uint8_t msgid) const { return rx_counts_[msgid]; }

private:
    // Tracks an outstanding command awaiting RSP, used for timeout/retransmit logic.
    struct PendingCmd {
        uint8_t msgid = 0;              // Original command message ID
        uint8_t seq = 0;                // Sequence number for RSP matching
        uint8_t retries = 0;            // Retransmit count (max 3)
        uint64_t deadline_us = 0;       // Timeout deadline in microseconds
        std::vector<uint8_t> bytes;     // Raw frame bytes for retransmission
    };

    void on_frame(const protocol::Frame &frame, uint64_t receive_time_us);
    void send_cmd(uint8_t msgid, const std::vector<uint8_t> &payload, uint64_t now_us);
    void handle_rsp(const protocol::Frame &frame, uint64_t receive_time_us);
    void handle_cfg_report(const protocol::Frame &frame);
    void handle_text_report(const protocol::Frame &frame);
    void handle_data_sample(const protocol::Frame &frame);

    sim::VirtualLinkEndpoint *endpoint_ = nullptr;
    protocol::Parser parser_;
    uint8_t cmd_seq_ = 0;
    std::map<uint8_t, PendingCmd> pending_;
    uint32_t current_lsb_nA_ = 1000;
    bool adcrange_ = false;
    uint16_t stream_period_us_ = 0;
    uint16_t stream_mask_ = 0;
    bool has_data_seq_ = false;
    uint8_t last_data_seq_ = 0;
    uint64_t data_drop_count_ = 0;
    uint64_t timeout_count_ = 0;
    uint64_t retransmit_count_ = 0;
    uint64_t rx_counts_[256] = {};
    uint64_t time_sync_T1_ = 0;  // T1 timestamp for TIME_SYNC calculation
    uint64_t current_time_us_ = 0;  // Current time updated in tick()
};

} // namespace node
