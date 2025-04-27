// time_sync.hpp
#ifndef TIME_SYNC_HPP
#define TIME_SYNC_HPP

#include <cstdint>

// Request packet structure (Master -> Slave)
struct TimeSyncRequest {
    uint16_t header;  // Fixed 0xAA55
    uint16_t seq;     // Sync sequence number
    uint64_t T1;      // Master send time (us)
    uint16_t crc;     // CRC checksum (covers previous fields)
} __attribute__((packed));

// Reply packet structure (Slave -> Master)
struct TimeSyncReply {
    uint16_t header;  // Fixed 0xAA55
    uint16_t seq;     // Sync sequence number
    uint64_t T1;      // Master send time (returned as-is)
    uint64_t T2;      // Slave receive time (us)
    uint64_t T3;      // Slave send time (us)
    uint16_t crc;     // CRC checksum (covers previous fields)
} __attribute__((packed));

// Command packet structure (Master -> Slave)
struct TimeSyncCmd {
    uint16_t header;  // Fixed 0xAA56
    uint16_t seq;     // Sync sequence number
    int64_t o;        // Master send offset time
    uint16_t crc;     // CRC checksum
} __attribute__((packed));

class TimeSync {
public:
    static uint16_t calc_crc(const uint8_t* data, uint32_t len);
};

#endif // TIME_SYNC_HPP
