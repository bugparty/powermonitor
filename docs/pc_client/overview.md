# PC Client (Serial) Design

Related code:
- `pc_client/`

Related docs:
- `docs/pc_client/power_bundle_schema.md`

This document defines the PC client behavior and integration plan for the INA228 power monitor.
It uses the `serial` library (wjwwood/serial) to connect to a USB CDC/UART device, perform the
protocol handshake, and stream data for local storage.

## Goals

- Auto-detect the target device by USB PID (and optional VID).
- Establish a serial connection, initialize the device, and begin streaming.
- Collect the data stream until the user requests stop.
- Persist results to a JSON file for later analysis.

## Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| `serial` (wjwwood/serial) | Serial port communication | Link as CMake dependency |
| `CLI11` (CLIUtils/CLI11) | Command line argument parsing | Header-only |
| `nlohmann/json` | JSON output | Header-only |
| `yaml-cpp` | YAML config parsing | Link as CMake dependency |

Open serial ports with a configurable baud rate (default: 115200).

## Threading Model

The client uses a dual-thread architecture for reliable high-frequency data capture.

```
┌─────────────────┐      ┌───────────────────┐
│   Read Thread   │─────▶│  Thread-safe      │
│  (Serial I/O)   │      │  Sample Queue     │
└─────────────────┘      └─────────┬─────────┘
                                   │
                                   ▼
┌─────────────────┐      ┌───────────────────┐
│   Main Thread   │◀─────│  Dequeue & Store  │
│ (User interaction)     └───────────────────┘
└─────────────────┘
```

### Read Thread

- Blocks on serial read, wakes immediately when data arrives
- Parses incoming frames using `protocol::Parser`
- Pushes complete samples to a thread-safe queue
- Runs until stop flag is set

### Main Thread

- Handles user input (Ctrl+C, interactive commands)
- Sends commands to device (PING, SET_CFG, STREAM_START, etc.)
- Periodically drains sample queue for storage
- Manages session lifecycle (connect, start, stop, save)

### Synchronization

- Sample queue protected by mutex
- Atomic stop flag for clean shutdown
- No lock contention on hot path (read thread only pushes, main thread only pops)

## CLI Interface

### Basic Usage

```bash
# Simplest usage (all defaults)
powermonitor

# Specify output file
powermonitor -o capture.json

# Specify config file and sampling parameters
powermonitor -c config.yaml --period 500 --mask 0x0F

# Interactive mode
powermonitor -i
```

### Command Line Arguments

| Short | Long | Description | Default |
|-------|------|-------------|---------|
| `-o` | `--output` | Output JSON file path | `./output/powermonitor_YYYYMMDD_HHMMSS.json` |
| `-c` | `--config` | YAML configuration file | None (use device config) |
| `-p` | `--port` | Serial port (e.g., COM5, /dev/ttyUSB0) | Auto-detect |
| `-b` | `--baud` | Baud rate | 115200 |
| | `--period` | Sampling period in microseconds | 1000 |
| | `--mask` | Channel mask | 0x0F (all channels) |
| | `--duration-s` | Auto-stop capture after N seconds | 0 (disabled) |
| | `--duration-us` | Auto-stop capture after N microseconds | 0 (disabled) |
| | `--run-label` | Run label stored in output metadata | Empty |
| | `--run-tag` | Repeatable run tag stored in output metadata | None |
| | `--usb-stress` | Enable USB throughput stress mode (sets STREAM_START mask bit15) | No |
| `-i` | `--interactive` | Enter interactive mode | No |
| `-v` | `--verbose` | Verbose output | No |
| `-h` | `--help` | Show help | - |
| | `--version` | Show version | - |

**Priority**: CLI arguments > YAML config > Defaults

**Default Device**: VID=0x2E8A, PID=0x000A (Raspberry Pi Pico)

### Default Output Path

By default, the client writes to `./output/` with the filename pattern:

```
powermonitor_YYYYMMDD_HHMMSS.json
```

If the directory does not exist, the client must create it.

### YAML Configuration File

```yaml
# config.yaml
stream:
  period_us: 1000
  mask: 0x0F
  usb_stress_mode: false

ina228:
  config_reg: 0x0000
  adc_config_reg: 0x1000
  shunt_cal: 4096
  shunt_tempco: 0  # optional
```

### Interactive Mode (FTXUI TUI)

In interactive mode, the client opens a full-screen FTXUI terminal UI for live monitoring and controls.

| Key | Description |
|-----|-------------|
| `t` | Toggle stream (`STREAM_START` / `STREAM_STOP`) |
| `s` | Save current snapshot to output JSON path |
| `q` | Quit session gracefully |

Displayed panels include:
- Connection/init/stream status
- Live counters (samples, RX/TX, CRC failures, timeouts, IO errors, queue overflow)
- Latest decoded sample summary
- Scrolling log area

Notes:
- In non-interactive mode, capture starts immediately after connection and runs until Ctrl+C.
- TUI mode is intended to work on both Windows and Linux terminals.

## Port Discovery and Connection

1. Enumerate available serial ports using `serial::list_ports()`.
2. Filter ports by USB PID (and optional VID).
3. If multiple matches exist, select the first match.
4. Open the port and configure serial settings:
   - Baud rate
   - Timeout (read/write)
   - Flow control (none)
5. Initialize the protocol session:
   - Send `PING`
   - Send `GET_CFG` to retrieve current device configuration
   - Send `SET_CFG` if user specifies custom parameters
   - Send `STREAM_START` with user-defined period/mask

### USB Throughput Stress Mode

- PC client can enable device stress mode with CLI flag `--usb-stress`.
- Internally this sets bit15 (`0x8000`) in `STREAM_START.channel_mask`.
- Device then emits fixed-value `DATA_SAMPLE` frames as fast as possible for throughput testing.
- Base channel mask bits remain available for normal channel semantics when stress mode is disabled.

### VID/PID Detection Notes

`serial::list_ports()` exposes a `hardware_id` string per port. Its format varies across
operating systems, so the client should use tolerant parsing rather than strict matching.

Typical examples:

- Windows (COM):
  - `USB\\VID_2E8A&PID_000A\\...`
  - `USB VID:PID=2E8A:000A SER=...`
- Linux (`/dev/ttyACM*`):
  - `USB VID:PID=2E8A:000A SER=...`
- macOS (`/dev/cu.usbmodem*`):
  - `USB VID:PID=2E8A:000A SER=...`

Recommended approach:
1. Normalize `hardware_id` to uppercase.
2. Parse either `USB\\VID_xxxx&PID_xxxx` or `VID:PID=xxxx:xxxx`.
3. If parsing fails:
   - If exactly one port exists, use it.
   - Otherwise, require an explicit `--port`.

The PID filter is mandatory; the VID filter is optional.

## Serial Connection Handling

### Connection Failure

| Scenario | Handling |
|----------|----------|
| No matching device | Continuously scan every 1 second until device is found or user cancels (Ctrl+C) |
| Multiple devices found | Select the first match |
| Port busy (occupied) | Notify user to close other programs using the port |
| Permission denied | Notify user to check permissions (e.g., add user to `dialout` group on Linux) |
| Open timeout | Notify user to check device |

### Disconnection During Session

| Scenario | Detection | Handling |
|----------|-----------|----------|
| Device unplugged | Read/write returns error | Save collected data and exit |
| Communication timeout | No DATA frame for 5 seconds | Save collected data and exit |
| Read/write error | System error code | Save collected data and exit |

Notes:
- No automatic reconnection. On disconnection, save data and exit gracefully.
- Communication timeout threshold is fixed at 5 seconds.
- The scan interval for device discovery is 1 second.

## Streaming Lifecycle

- Receive frames using the existing `protocol::Parser`.
- On `CFG_REPORT`, store `current_lsb_nA` and `adcrange` for engineering conversion.
- On `DATA_SAMPLE`, parse and append to a local record list.
- Monitor for user stop request:
  - CLI signal (e.g., Ctrl+C)
  - Explicit command (e.g., input or timeout)
- On stop:
  - Send `STREAM_STOP`
  - Flush and close serial port
  - Write output JSON file

## Error Handling (RSP Status Codes)

When a command receives a non-OK RSP status, the client must handle it according to the table below.

### Status Code Reference

| Value | Name | Description |
|-------|------|-------------|
| `0x00` | OK | Success |
| `0x01` | ERR_CRC | CRC verification failed |
| `0x02` | ERR_LEN | Invalid packet length |
| `0x03` | ERR_UNK_CMD | Unknown or unsupported MSGID |
| `0x04` | ERR_PARAM | Parameter out of range |
| `0x05` | ERR_HW | Hardware fault (e.g., I2C NACK) |

### Per-Command Error Handling

#### PING (0x01)

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Log error, terminate session |
| ERR_UNK_CMD | Terminate session, prompt user to upgrade firmware |
| ERR_PARAM | Log (should not occur) |
| ERR_HW | Log (should not occur) |

#### SET_CFG (0x10)

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Log error, terminate session |
| ERR_UNK_CMD | Terminate session, prompt user to upgrade firmware |
| ERR_PARAM | Notify user of invalid parameter, send GET_CFG to confirm current config |
| ERR_HW | Notify user of hardware issue, send GET_CFG to confirm current config |

#### GET_CFG (0x11)

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Log error, terminate session |
| ERR_UNK_CMD | Terminate session, prompt user to upgrade firmware |
| ERR_PARAM | Log (should not occur) |
| ERR_HW | Terminate session (config required for data conversion) |

#### STREAM_START (0x30)

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Log error, terminate session |
| ERR_UNK_CMD | Terminate session, prompt user to upgrade firmware |
| ERR_PARAM | Notify user of invalid parameter, allow retry with different parameters |
| ERR_HW | Terminate session |

#### STREAM_STOP (0x31)

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry once, then save data and exit |
| ERR_LEN | Retry once, then save data and exit |
| ERR_UNK_CMD | Retry once, then save data and exit |
| ERR_PARAM | Retry once, then save data and exit |
| ERR_HW | Retry once, then save data and exit |

Note: STREAM_STOP errors should not block the exit flow. Collected data must be saved regardless of stop success.

#### REG_READ (0x20) - Debug Command

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Notify user |
| ERR_UNK_CMD | Notify user (firmware may not support debug commands) |
| ERR_PARAM | Notify user of invalid parameter |
| ERR_HW | Notify user of hardware issue |

Debug command failures do not terminate the session.

#### REG_WRITE (0x21) - Debug Command

| Error | Handling |
|-------|----------|
| ERR_CRC | Retry up to 3 times |
| ERR_LEN | Notify user |
| ERR_UNK_CMD | Notify user (firmware may not support debug commands) |
| ERR_PARAM | Notify user of invalid parameter |
| ERR_HW | Notify user of hardware issue |

Debug command failures do not terminate the session.

### Summary Table

| Command | ERR_CRC | ERR_LEN | ERR_UNK_CMD | ERR_PARAM | ERR_HW |
|---------|---------|---------|-------------|-----------|--------|
| PING | Retry 3x | Terminate | Terminate | Log | Log |
| SET_CFG | Retry 3x | Terminate | Terminate | Notify + GET_CFG | Notify + GET_CFG |
| GET_CFG | Retry 3x | Terminate | Terminate | Log | Terminate |
| STREAM_START | Retry 3x | Terminate | Terminate | Notify, allow retry | Terminate |
| STREAM_STOP | Retry 1x, save & exit | Retry 1x, save & exit | Retry 1x, save & exit | Retry 1x, save & exit | Retry 1x, save & exit |
| REG_READ | Retry 3x | Notify | Notify | Notify | Notify |
| REG_WRITE | Retry 3x | Notify | Notify | Notify | Notify |

## Logging and Metrics

Report the following during runtime and at completion:

- RX/TX counts per MSGID
- CRC failure count
- Data frame loss count (SEQ gaps)
- Timeout and retransmission counts

## Output (JSON Schema)

The client must write collected samples to a JSON file when the session ends.
The JSON structure is an object with a `meta` block and a `samples` array.

### Top-Level

```json
{
  "meta": { },
  "samples": [ ]
}
```

### Meta

```json
{
  "schema_version": "1.0",
  "protocol_version": 1,
  "device": {
    "vid": "0x0000",
    "pid": "0x0000",
    "port": "COM5",
    "baud": 115200
  },
  "session": {
    "start_time_utc": "2025-01-01T00:00:00Z",
    "end_time_utc": "2025-01-01T00:00:10Z",
    "duration_us": 10000000
  },
  "config": {
    "stream_period_us": 1000,
    "stream_mask": 15,
    "current_lsb_nA": 1000,
    "adcrange": 0,
    "shunt_cal": 4096,
    "config_reg": 0,
    "adc_config_reg": 4096
  },
  "stats": {
    "rx_counts": { "0x80": 10000, "0x91": 1 },
    "crc_fail": 0,
    "data_drop": 0,
    "timeouts": 0,
    "retries": 0
  }
}
```

### Samples

Each sample includes host receive time (relative to session start) and device timestamp.
Raw values and engineering values are both preserved.

```json
{
  "seq": 42,
  "timestamp_us": 1234567,
  "device_timestamp_us": 1234000,
  "flags": 0,
  "raw": {
    "vbus_u20": 123456,
    "vshunt_s20": -123,
    "current_s20": 456,
    "dietemp_s16": 4500
  },
  "engineering": {
    "vbus_v": 12.001,
    "vshunt_v": 0.00012,
    "current_a": 0.456,
    "temp_c": 35.1
  }
}
```

Notes:
- `timestamp_us` (u64): Host receive time in microseconds, relative to session start (first sample = 0).
- `device_timestamp_us` (u64): Device-provided timestamp, converted to absolute time by PC (see below).
- The sample rate target is 1 kHz; host timing jitter is expected to be under 1 ms.
- `meta.session.start_time_utc` serves as the absolute time anchor for both timestamps.

### Timestamp Handling

The device `timestamp_us` is a 32-bit value that overflows after approximately 71.6 minutes.
The PC client must detect overflow and convert to 64-bit absolute time.

**Overflow Detection Algorithm**:

```cpp
// State variables (per session)
uint32_t last_device_ts = 0;
uint64_t overflow_count = 0;

// Called for each DATA_SAMPLE received
uint64_t process_device_timestamp(uint32_t ts_us) {
    if (ts_us < last_device_ts) {
        // Wrap-around detected
        overflow_count++;
    }
    last_device_ts = ts_us;

    // Return absolute time in microseconds
    return (overflow_count << 32) | ts_us;
}
```

**Time Reference**:

| Field | Type | Description |
|-------|------|-------------|
| `meta.session.start_time_utc` | string | PC system time when session started (ISO 8601) |
| `timestamp_us` | u64 | Host receive time relative to session start |
| `device_timestamp_us` | u64 | Device time relative to STREAM_START, overflow-corrected |

This design allows:
- Correlation between host and device timing (jitter analysis)
- Sessions longer than 71.6 minutes
- Post-processing with absolute time reference from `start_time_utc`

## Build Configuration (CMake)

### Project Structure

```
powermonitor/
├── protocol/              # Shared protocol library
├── pc_sim/                # Simulator and tests
├── pc_client/             # PC client (new)
│   ├── CMakeLists.txt
│   └── src/
└── CMakeLists.txt         # Top-level, includes all subdirectories
```

### Dependencies (FetchContent)

```cmake
include(FetchContent)

FetchContent_Declare(serial
  GIT_REPOSITORY https://github.com/wjwwood/serial.git
  GIT_TAG 1.2.1)

FetchContent_Declare(cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)

FetchContent_Declare(yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0)

FetchContent_MakeAvailable(cli11 nlohmann_json yaml-cpp)

FetchContent_GetProperties(serial)
if (NOT serial_POPULATED)
  FetchContent_Populate(serial)
endif()

add_library(serial
  ${serial_SOURCE_DIR}/src/serial.cc
  ${serial_SOURCE_DIR}/src/impl/unix.cc
  ${serial_SOURCE_DIR}/src/impl/list_ports/list_ports_linux.cc)
target_include_directories(serial PUBLIC ${serial_SOURCE_DIR}/include)
```

### Targets

| Target | Type | Description |
|--------|------|-------------|
| `powermonitor_protocol` | STATIC | Shared protocol library (parser, frame builder, CRC) |
| `host_pc_client` | EXECUTABLE | PC client application (FTXUI-capable) |

### Example pc_client/CMakeLists.txt

```cmake
project(powermonitor_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(host_pc_client
    src/main.cpp
)

target_link_libraries(host_pc_client
    powermonitor_protocol
    serial
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    yaml-cpp
    ftxui::screen
    ftxui::dom
    ftxui::component
)
```

### Build Commands

```bash
# Configure
cmake -B build_client -S .

# Build
cmake --build build_client --target host_pc_client

# Run tests
cmake --build build_client --target powermonitor_client_test
ctest --test-dir build_client
```
