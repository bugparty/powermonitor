# Time Synchronization Protocol Documentation

Related code:
- `device/time_sync.cpp`
- `device/time_sync.hpp`
- `device/timesync.py`

This document describes the time synchronization protocol implemented between a host computer (Master) and a Raspberry Pi Pico device (Slave) over a serial connection.

## Overview

The protocol implements a precise time synchronization mechanism similar to NTP (Network Time Protocol) but optimized for serial connections. It allows the slave device to synchronize its clock with the master with microsecond precision.

## Packet Structure

### Time Sync Request (Master → Slave)
```
struct TimeSyncRequest {
    uint16_t header;  // Fixed 0xAA55
    uint16_t seq;     // Sync sequence number
    uint64_t T1;      // Master send time (us)
    uint16_t crc;     // CRC checksum (covers previous fields)
} __attribute__((packed));
```

### Time Sync Reply (Slave → Master)
```
struct TimeSyncReply {
    uint16_t header;  // Fixed 0xAA55
    uint16_t seq;     // Sync sequence number
    uint64_t T1;      // Master send time (returned as-is)
    uint64_t T2;      // Slave receive time (us)
    uint64_t T3;      // Slave send time (us)
    uint16_t crc;     // CRC checksum (covers previous fields)
} __attribute__((packed));
```

### Time Sync Command (Master → Slave)
```
struct TimeSyncCmd {
    uint16_t header;  // Fixed 0xAA56
    uint16_t seq;     // Sync sequence number
    int64_t o;        // Master send offset time
    uint16_t crc;     // CRC checksum
} __attribute__((packed));
```

## Protocol Flow

1. **Master** generates a timestamp T1 and sends a Time Sync Request
2. **Slave** receives the request at timestamp T2
3. **Slave** generates timestamp T3 and sends a Time Sync Reply with T1, T2, T3
4. **Master** receives the reply at timestamp T4 (locally calculated)
5. **Master** calculates:
   - Network delay: `delay = (T4 - T1) - (T3 - T2)`
   - Clock offset: `offset = ((T2 - T1) + (T3 - T4)) / 2`
6. **Master** sends a Time Sync Command with the negative offset
7. **Slave** adjusts its clock by adding the received offset

## Special Commands

- When `seq = 0`, the Master can set the absolute Unix time on the Slave
- The Slave maintains a global epoch offset that is adjusted with each sync command

## Error Handling

- CRC16 calculation ensures data integrity
- Magic bytes detection (0xAA55, 0xAA56) ensures proper packet framing
- Timeout handling with retries ensures robust operation
- Consecutive error counting prevents system flooding during connection issues

## Implementation Notes

### Master Side (Python)
- Uses monotonic clock to track time consistently
- Logs all transactions for debugging and analysis
- Implements error recovery strategies
- Provides command-line interface for configuration

### Slave Side (C++ on Pico)
- Maintains an epoch offset that is adjusted with each sync
- Provides a `get_unix_time_us()` function for application use
- Validates all incoming packets with CRC checks
- Uses low-level serial I/O for efficient communication

## Usage Recommendations

1. Run synchronization at regular intervals (e.g., every second)
2. Log offset values to monitor system stability
3. Consider environmental factors that might affect timing precision
4. For critical applications, implement a sliding window average of offsets

## Performance Characteristics

- Typical precision: 10-100 microseconds
- Affected by serial connection quality and system load
- More frequent synchronization improves stability but increases overhead
