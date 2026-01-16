#include "protocol/frame_builder.h"

#include "protocol/crc16_ccitt_false.h"

namespace protocol {

std::vector<uint8_t> build_frame(FrameType type, uint8_t flags, uint8_t seq, uint8_t msgid,
                                 const std::vector<uint8_t> &data) {
    const uint16_t len = static_cast<uint16_t>(1 + data.size());
    std::vector<uint8_t> frame;
    frame.reserve(2 + 6 + len + 2);
    frame.push_back(kSof0);
    frame.push_back(kSof1);
    frame.push_back(kProtoVersion);
    frame.push_back(static_cast<uint8_t>(type));
    frame.push_back(flags);
    frame.push_back(seq);
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(msgid);
    frame.insert(frame.end(), data.begin(), data.end());

    const uint16_t crc = crc16_ccitt_false(&frame[2], 6 + len);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

} // namespace protocol
