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
    void send_set_cfg(uint16_t config_reg, uint16_t adc_config_reg, uint16_t shunt_cal,
                      uint16_t shunt_tempco, uint64_t now_us);
    void send_get_cfg(uint64_t now_us);
    void send_stream_start(uint16_t period_us, uint16_t mask, uint64_t now_us);
    void send_stream_stop(uint64_t now_us);

    uint64_t crc_fail_count() const { return parser_.crc_fail_count(); }
    uint64_t data_drop_count() const { return data_drop_count_; }
    uint64_t timeout_count() const { return timeout_count_; }
    uint64_t retransmit_count() const { return retransmit_count_; }

private:
    struct PendingCmd {
        uint8_t msgid = 0;
        uint8_t seq = 0;
        uint8_t retries = 0;
        uint64_t deadline_us = 0;
        std::vector<uint8_t> bytes;
    };

    void on_frame(const protocol::Frame &frame);
    void send_cmd(uint8_t msgid, const std::vector<uint8_t> &payload, uint64_t now_us);
    void handle_rsp(const protocol::Frame &frame);
    void handle_cfg_report(const protocol::Frame &frame);
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
};

} // namespace node
