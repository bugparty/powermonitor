#pragma once

#include "protocol_constants.h"
#include <cstdint>
#include <vector>

namespace protocol {

struct Frame {
    uint8_t ver = kProtoVersion;
    FrameType type = FrameType::kCmd;
    uint8_t flags = 0;
    uint8_t seq = 0;
    uint16_t len = 0;
    uint8_t msgid = 0;
    std::vector<uint8_t> data;
};

// Build a complete protocol frame with SOF, header, payload, and CRC16.
std::vector<uint8_t> build_frame(FrameType type, uint8_t flags, uint8_t seq, uint8_t msgid,
                                 const std::vector<uint8_t> &data);

} // namespace protocol
