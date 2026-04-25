# PC Client Time Sync Demo (Real Device)

## Purpose

This demo exercises the time synchronization protocol between a PC host and a Raspberry Pi Pico device over a real USB serial link. It validates that the NTP-like time sync mechanism works correctly on real hardware, measuring clock offset and drift.

## What It Tests

- **TIME_SET (0x07)**: Sets the device's epoch offset using the PC's Unix time.
- **TIME_SYNC (0x05)**: Performs NTP-like round-trip time measurement (T1/T2/T3/T4).
- **TIME_ADJUST (0x06)**: Applies the calculated offset to the device's `epoch_offset_us`.
- **Drift measurement**: After initial sync, waits for defined intervals (2/5/10 min) and measures residual offset to characterize the device's clock drift.

## Protocol Summary

| Step | PC sends | Device returns |
|------|----------|----------------|
| 1 | `TIME_SET` with Unix time | `RSP(OK)` |
| 2 | `TIME_SYNC` with T1 (PC send time) | `RSP(OK, T1, T2, T3)` where T2=device receive, T3=device send |
| 3 | PC captures T4 (receive time), computes offset | — |
| 4 | `TIME_ADJUST` with `-offset` | `RSP(OK)` |

**Formulas** (per `docs/protocol/uart_protocol.md`):

```
delay  = (T4 - T1) - (T3 - T2)
offset = ((T2 - T1) + (T3 - T4)) / 2
```

## Running the Demo

### Build

```bash
cmake -B build -S .
cmake --build build --target pc_time_sync_demo
```

### Run

```bash
# Auto-detect device (VID/PID)
./build/pc_client/Debug/pc_time_sync_demo.exe

# Explicit port
./build/pc_client/Debug/pc_time_sync_demo.exe --port COM7

# Custom intervals
./build/pc_client/Debug/pc_time_sync_demo.exe --port COM7 --interval_ms 500
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | auto | Serial port (e.g., `COM7` or `/dev/ttyUSB0`) |
| `--baud` | 115200 | Baud rate |
| `--sync_count` | 10 | Number of initial sync rounds |
| `--interval_ms` | 1000 | Interval between initial sync rounds |
| `--cmd_timeout_ms` | 1000 | Command response timeout |
| `--do_time_set` | true | Send TIME_SET at startup |
| `--vid` | 0x2E8A | USB VID filter for auto-discovery |
| `--pid` | 0x000A | USB PID filter for auto-discovery |

### Long-Duration Drift Test

The demo automatically runs a drift test sequence:

1. **Phase 1**: 10 rounds fast sync (1s interval)
2. **Phase 2**: Wait 2 minutes → 1 sync → record offset
3. **Phase 3**: Wait 5 minutes → 10 rounds fast sync
4. **Phase 4**: Wait 10 minutes → 1 sync → record offset

Expected output:

```
Drift Results:
  2 min: offset=xxx us
  10 min: offset=xxx us
```

## Expected Results

- Initial offset after `TIME_SET`: large (depends on PC/device uptime difference)
- After first `TIME_SYNC` + `TIME_ADJUST`: < 1 ms typical
- Drift over 10 minutes: typically < 100 µs for a well-compensated crystal, but varies by hardware

## Related Files

- Demo source: [`pc_client/src/time_sync_demo.cpp`](../../pc_client/src/time_sync_demo.cpp)
- Device implementation: [`device/command_handler.hpp`](../../device/command_handler.hpp) (TIME_SYNC handler)
- Protocol spec: [`docs/protocol/uart_protocol.md`](../protocol/uart_protocol.md)
