#pragma once

#include <cstdint>
#include <cstddef>

namespace protocol {

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len, uint16_t initial_crc = 0xFFFF);

} // namespace protocol
