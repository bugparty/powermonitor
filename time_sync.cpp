
// time_sync.cpp
#include "time_sync.hpp"

uint16_t TimeSync::calc_crc(const uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc = (crc >> 1);
        }
    }
    return crc;
}

