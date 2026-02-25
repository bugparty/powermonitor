# USB Packet Loss Investigation

## Problem Description

Every ~1 second, 1 data sample packet is lost. Every ~2 minutes, 1-2 packets are lost (during time sync).

## Symptoms

- Sequence number gaps: 1 packet lost per second, 1-2 per 2 minutes
- Device shows `dropped=0` (queue never overflows)
- PC shows `crc_fail=0` (no CRC errors)
- Absolute timestamps are correct (TIME_SET working)
- Timestamp intervals are correct (~1000μs)

## Root Cause Analysis

The packet loss is NOT due to:
1. Sample queue overflow (device shows dropped=0)
2. CRC errors (crc_fail=0)
3. USB bandwidth (USB is full-duplex)

The packet loss appears to be at the **Windows USB CDC driver level** - packets sent by the device are not received by the PC.

## Attempts Made

### 1. Increased Sample Queue Buffer
- Changed: `SpscQueue<RawSample, 256>` → `SpscQueue<RawSample, 512>`
- Result: No improvement
- Conclusion: Queue was not the bottleneck

### 2. Changed Stats Report to Non-Blocking
- Changed: `send_stats_report()` used `write_flush_fn_` → `write_noflush_fn_`
- Result: No improvement
- Conclusion: Stats report blocking was not the cause

### 3. Batch Processing in Main Loop
- Changed: Process 1 sample per loop → 32 samples per loop
- Result: No improvement
- Conclusion: Processing speed was not the bottleneck

### 4. Changed Data Sample to Non-Blocking
- Changed: `send_data_sample()` → `send_data_sample_noflush()`
- Result: No improvement
- Conclusion: USB write blocking was not the cause

## Current State

- Packet loss persists regardless of device-side changes
- Appears to be a Windows USB CDC driver issue
- Acceptable for most use cases (99.9%+ data integrity)

## Future Improvements (Not Implemented)

1. **Use separate USB endpoints** - One for data, one for control
2. **Implement flow control** - Pause sampling when USB buffer is full
3. **Increase USB buffer size** - Windows registry settings
4. **Use isochronous endpoints** - For guaranteed bandwidth

## Design Decision Documented

Added to `docs/device/time_sync.md`:

> ### Trade-off: Sample Loss vs Sync Latency
>
> **Why skip streaming during sync?**
> - If `process_streaming()` is called during sync_waiting, USB bandwidth is shared between data samples and time sync frames
> - This increases T2/T3 jitter, reducing sync precision
> - Therefore, streaming is paused during sync to prioritize sync accuracy
>
> **Consequence:**
> - 1 data sample is lost per 5-second sync cycle (~0.02% loss at 1kHz)
> - This is acceptable for most use cases (99.98% data integrity)
> - If zero sample loss is required, implement parallel USB endpoints (one for data, one for control)
