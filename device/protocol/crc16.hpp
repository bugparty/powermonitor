#ifndef DEVICE_PROTOCOL_CRC16_HPP
#define DEVICE_PROTOCOL_CRC16_HPP

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

} // namespace protocol

#endif // DEVICE_PROTOCOL_CRC16_HPP
