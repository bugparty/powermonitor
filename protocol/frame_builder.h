#pragma once

#include <cstdint>
#include <vector>

namespace protocol {

constexpr uint8_t kSof0 = 0xAA;
constexpr uint8_t kSof1 = 0x55;
constexpr uint8_t kProtoVersion = 0x01;

enum class FrameType : uint8_t {
    kCmd = 0x01,
    kRsp = 0x02,
    kData = 0x03,
    kEvt = 0x04,
};

struct Frame {
    uint8_t ver = kProtoVersion;
    FrameType type = FrameType::kCmd;
    uint8_t flags = 0;
    uint8_t seq = 0;
    uint16_t len = 0;
    uint8_t msgid = 0;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> build_frame(FrameType type, uint8_t flags, uint8_t seq, uint8_t msgid,
                                 const std::vector<uint8_t> &data);

} // namespace protocol
