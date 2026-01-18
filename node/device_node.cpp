#include "node/device_node.h"

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

void append_u16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void append_u32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8U);
}

uint32_t pack_u20(uint32_t value, uint8_t out[3]) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 16) & 0x0F);
    return value;
}

void pack_s20(int32_t value, uint8_t out[3]) {
    const uint32_t raw = static_cast<uint32_t>(value) & 0xFFFFF;
    out[0] = static_cast<uint8_t>(raw & 0xFF);
    out[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((raw >> 16) & 0x0F);
}

} // namespace

DeviceNode::DeviceNode(sim::VirtualLinkEndpoint *endpoint)
    : endpoint_(endpoint),
      parser_([this](const protocol::Frame &frame) { on_frame(frame); }) {}

void DeviceNode::tick(uint64_t now_us) {
    current_now_us_ = now_us;
    if (!initial_cfg_sent_) {
        send_cfg_report(now_us);
        initial_cfg_sent_ = true;
    }
    if (endpoint_ && endpoint_->available() > 0) {
        auto bytes = endpoint_->read(endpoint_->available());
        parser_.feed(bytes);
    }

    if (streaming_on_ && now_us >= next_sample_due_us_) {
        send_data_sample(now_us);
        next_sample_due_us_ += stream_period_us_;
    }
}

void DeviceNode::on_frame(const protocol::Frame &frame) {
    ++rx_counts_[frame.msgid];
    if (frame.type == protocol::FrameType::kCmd) {
        handle_cmd(frame, current_now_us_);
    }
}

void DeviceNode::handle_cmd(const protocol::Frame &frame, uint64_t now_us) {
    switch (frame.msgid) {
    case kMsgPing: {
        send_rsp(frame.seq, frame.msgid, kStatusOk, {}, now_us);
        break;
    }
    case kMsgSetCfg: {
        if (frame.data.size() >= 8) {
            config_reg_ = read_u16(frame.data, 0);
            adc_config_reg_ = read_u16(frame.data, 2);
            shunt_cal_reg_ = read_u16(frame.data, 4);
            shunt_tempco_ = read_u16(frame.data, 6);
            current_lsb_nA_ = shunt_cal_reg_ == 0 ? 1000 : current_lsb_nA_;
        }
        send_rsp(frame.seq, frame.msgid, kStatusOk, {}, now_us);
        send_cfg_report(now_us);
        break;
    }
    case kMsgGetCfg: {
        send_rsp(frame.seq, frame.msgid, kStatusOk, {}, now_us);
        send_cfg_report(now_us);
        break;
    }
    case kMsgStreamStart: {
        if (frame.data.size() >= 4) {
            stream_period_us_ = read_u16(frame.data, 0);
            stream_mask_ = read_u16(frame.data, 2);
            streaming_on_ = true;
            stream_start_us_ = now_us;
            next_sample_due_us_ = now_us + stream_period_us_;
        }
        send_rsp(frame.seq, frame.msgid, kStatusOk, {}, now_us);
        break;
    }
    case kMsgStreamStop: {
        streaming_on_ = false;
        send_rsp(frame.seq, frame.msgid, kStatusOk, {}, now_us);
        break;
    }
    default:
        send_rsp(frame.seq, frame.msgid, 0x03, {}, now_us);
        break;
    }
}

void DeviceNode::send_rsp(uint8_t seq, uint8_t orig_msgid, uint8_t status,
                          const std::vector<uint8_t> &data, uint64_t now_us) {
    std::vector<uint8_t> payload;
    payload.push_back(orig_msgid);
    payload.push_back(status);
    payload.insert(payload.end(), data.begin(), data.end());
    auto bytes = protocol::build_frame(protocol::FrameType::kRsp, 0, seq, orig_msgid, payload);
    endpoint_->write(bytes, now_us);
}

void DeviceNode::send_cfg_report(uint64_t now_us) {
    std::vector<uint8_t> payload;
    payload.push_back(protocol::kProtoVersion);
    uint8_t flags = 0;
    if (streaming_on_) {
        flags |= 0x01;
    }
    if (shunt_cal_reg_ != 0) {
        flags |= 0x02;
    }
    if (adcrange_) {
        flags |= 0x04;
    }
    payload.push_back(flags);
    append_u32(payload, current_lsb_nA_);
    append_u16(payload, shunt_cal_reg_);
    append_u16(payload, config_reg_);
    append_u16(payload, adc_config_reg_);
    append_u16(payload, stream_period_us_);
    append_u16(payload, stream_mask_);

    const uint8_t seq = data_seq_++;
    auto bytes = protocol::build_frame(protocol::FrameType::kEvt, 0, seq, kMsgCfgReport, payload);
    endpoint_->write(bytes, now_us);
    std::cout << "DEV CFG_REPORT sent\n";
}

void DeviceNode::send_data_sample(uint64_t now_us) {
    const auto raw = model_.sample(now_us, current_lsb_nA_, adcrange_);
    std::vector<uint8_t> payload;
    const uint32_t timestamp = static_cast<uint32_t>(now_us - stream_start_us_);
    append_u32(payload, timestamp);
    uint8_t flags = 0;
    flags |= 0x01;
    if (shunt_cal_reg_ != 0) {
        flags |= 0x04;
    }
    payload.push_back(flags);

    uint8_t buf[3] = {};
    pack_u20(raw.vbus_raw, buf);
    payload.insert(payload.end(), buf, buf + 3);
    pack_s20(raw.vshunt_raw, buf);
    payload.insert(payload.end(), buf, buf + 3);
    pack_s20(raw.current_raw, buf);
    payload.insert(payload.end(), buf, buf + 3);
    append_u16(payload, static_cast<uint16_t>(raw.temp_raw));

    const uint8_t seq = data_seq_++;
    auto bytes = protocol::build_frame(protocol::FrameType::kData, 0, seq, kMsgDataSample, payload);
    endpoint_->write(bytes, now_us);
}

} // namespace node
