#include "node/pc_node.h"

#include <cstring>
#include <iostream>

namespace node {

namespace {

constexpr uint8_t kMsgPing = 0x01;
constexpr uint8_t kMsgSetCfg = 0x10;
constexpr uint8_t kMsgGetCfg = 0x11;
constexpr uint8_t kMsgStreamStart = 0x30;
constexpr uint8_t kMsgStreamStop = 0x31;
constexpr uint8_t kMsgDataSample = 0x80;
constexpr uint8_t kMsgCfgReport = 0x91;
constexpr uint8_t kStatusOk = 0x00;
constexpr uint64_t kCmdTimeoutUs = 200000;
constexpr uint8_t kMaxRetries = 3;

void append_u16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8U);
}

uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<uint32_t>(data[offset + 3]) << 24U);
}

} // namespace

PCNode::PCNode(sim::VirtualLinkEndpoint *endpoint)
    : endpoint_(endpoint),
      parser_([this](const protocol::Frame &frame) { on_frame(frame); }) {}

void PCNode::tick(uint64_t now_us) {
    if (endpoint_ && endpoint_->available() > 0) {
        auto bytes = endpoint_->read(endpoint_->available());
        parser_.feed(bytes);
    }

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now_us >= it->second.deadline_us) {
            if (it->second.retries >= kMaxRetries) {
                ++timeout_count_;
                std::cout << "CMD timeout msgid=0x" << std::hex
                          << static_cast<int>(it->second.msgid) << std::dec << "\n";
                it = pending_.erase(it);
                continue;
            }
            ++retransmit_count_;
            it->second.retries++;
            it->second.deadline_us = now_us + kCmdTimeoutUs;
            endpoint_->write(it->second.bytes, now_us);
            std::cout << "CMD retry msgid=0x" << std::hex << static_cast<int>(it->second.msgid)
                      << " seq=" << static_cast<int>(it->second.seq) << std::dec << "\n";
        }
        ++it;
    }
}

void PCNode::send_ping(uint64_t now_us) { send_cmd(kMsgPing, {}, now_us); }

void PCNode::send_set_cfg(uint16_t config_reg, uint16_t adc_config_reg, uint16_t shunt_cal,
                          uint16_t shunt_tempco, uint64_t now_us) {
    std::vector<uint8_t> payload;
    append_u16(payload, config_reg);
    append_u16(payload, adc_config_reg);
    append_u16(payload, shunt_cal);
    append_u16(payload, shunt_tempco);
    send_cmd(kMsgSetCfg, payload, now_us);
}

void PCNode::send_get_cfg(uint64_t now_us) { send_cmd(kMsgGetCfg, {}, now_us); }

void PCNode::send_stream_start(uint16_t period_us, uint16_t mask, uint64_t now_us) {
    std::vector<uint8_t> payload;
    append_u16(payload, period_us);
    append_u16(payload, mask);
    send_cmd(kMsgStreamStart, payload, now_us);
}

void PCNode::send_stream_stop(uint64_t now_us) { send_cmd(kMsgStreamStop, {}, now_us); }

void PCNode::send_cmd(uint8_t msgid, const std::vector<uint8_t> &payload, uint64_t now_us) {
    const uint8_t seq = cmd_seq_++;
    auto bytes = protocol::build_frame(protocol::FrameType::kCmd, 0, seq, msgid, payload);
    endpoint_->write(bytes, now_us);
    PendingCmd pending;
    pending.msgid = msgid;
    pending.seq = seq;
    pending.deadline_us = now_us + kCmdTimeoutUs;
    pending.bytes = bytes;
    pending_[seq] = pending;
    std::cout << "CMD send msgid=0x" << std::hex << static_cast<int>(msgid)
              << " seq=" << static_cast<int>(seq) << std::dec << "\n";
}

void PCNode::on_frame(const protocol::Frame &frame) {
    ++rx_counts_[frame.msgid];
    if (frame.type == protocol::FrameType::kRsp) {
        handle_rsp(frame);
        return;
    }
    if (frame.type == protocol::FrameType::kEvt && frame.msgid == kMsgCfgReport) {
        handle_cfg_report(frame);
        return;
    }
    if (frame.type == protocol::FrameType::kData && frame.msgid == kMsgDataSample) {
        handle_data_sample(frame);
        return;
    }
}

void PCNode::handle_rsp(const protocol::Frame &frame) {
    if (frame.data.size() < 2) {
        return;
    }
    const uint8_t orig_msgid = frame.data[0];
    const uint8_t status = frame.data[1];
    auto it = pending_.find(frame.seq);
    if (it == pending_.end()) {
        return;
    }
    if (it->second.msgid != orig_msgid) {
        return;
    }
    pending_.erase(it);
    std::cout << "RSP msgid=0x" << std::hex << static_cast<int>(orig_msgid)
              << " status=0x" << static_cast<int>(status) << std::dec << "\n";
    if (status != kStatusOk) {
        return;
    }
}

void PCNode::handle_cfg_report(const protocol::Frame &frame) {
    if (frame.data.size() < 16) {
        return;
    }
    const uint8_t proto_ver = frame.data[0];
    const uint8_t flags = frame.data[1];
    current_lsb_nA_ = read_u32(frame.data, 2);
    const uint16_t shunt_cal = read_u16(frame.data, 6);
    const uint16_t config_reg = read_u16(frame.data, 8);
    const uint16_t adc_config_reg = read_u16(frame.data, 10);
    stream_period_us_ = read_u16(frame.data, 12);
    stream_mask_ = read_u16(frame.data, 14);
    adcrange_ = (flags & 0x04) != 0;
    std::cout << "CFG_REPORT ver=" << static_cast<int>(proto_ver)
              << " lsb_nA=" << current_lsb_nA_ << " shunt_cal=" << shunt_cal
              << " config=0x" << std::hex << config_reg << " adc=0x" << adc_config_reg
              << " period_us=" << std::dec << stream_period_us_ << " mask=0x" << std::hex
              << stream_mask_ << std::dec << "\n";
}

void PCNode::handle_data_sample(const protocol::Frame &frame) {
    if (frame.data.size() < 16) {
        return;
    }
    if (!has_data_seq_) {
        has_data_seq_ = true;
        last_data_seq_ = frame.seq;
    } else {
        const uint8_t expected = static_cast<uint8_t>(last_data_seq_ + 1);
        if (frame.seq != expected) {
            ++data_drop_count_;
        }
        last_data_seq_ = frame.seq;
    }

    const uint32_t timestamp = read_u32(frame.data, 0);
    const uint8_t flags = frame.data[4];
    const uint8_t *vbus20 = &frame.data[5];
    const uint8_t *vshunt20 = &frame.data[8];
    const uint8_t *current20 = &frame.data[11];
    const int16_t temp_raw = static_cast<int16_t>(read_u16(frame.data, 14));

    const uint32_t vbus_raw = protocol::unpack_u20(vbus20);
    const int32_t vshunt_raw = protocol::unpack_s20(vshunt20);
    const int32_t current_raw = protocol::unpack_s20(current20);
    auto sample = protocol::to_engineering(vbus_raw, vshunt_raw, current_raw, temp_raw,
                                           current_lsb_nA_, adcrange_);

    if ((timestamp % 1000000U) < 1000U) {
        std::cout << "DATA ts_us=" << timestamp << " flags=0x" << std::hex
                  << static_cast<int>(flags) << std::dec << " vbus=" << sample.vbus_v
                  << " current=" << sample.current_a << " temp=" << sample.temp_c << "\n";
    }
}

} // namespace node
