# PC Simulator Tests

Related code:
- `pc_sim/pc_sim_main.cpp`

This document describes the test scenarios executed by the PC-side protocol simulator (`pc_sim/pc_sim_main.cpp`).

## Overview

The PC simulator (`pc_sim/pc_sim_main.cpp`) performs end-to-end integration testing of the communication protocol between a PC node and a device node. It simulates a complete power monitoring session with configurable fault injection.

## Test Scenario

The simulator executes a complete power monitoring workflow that exercises all major protocol features:

### 1. **Connection Establishment (PING)**

```cpp
pc.send_ping(loop.now_us());
```

**Purpose**: Verify basic connectivity between PC and device.

**Validates**:
- Command frame transmission
- Response frame reception
- CRC validation
- Sequence number handling

---

### 2. **Configuration Setup (SET_CFG)**

```cpp
pc.send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
```

**Purpose**: Configure the INA228 sensor parameters.

**Parameters**:
- `shunt_cal`: 0x0000 (default calibration)
- `config`: 0x0000 (default configuration)
- `adc`: 0x1000 (ADC settings)
- `diag_alert`: 0x0000 (diagnostics/alerts disabled)

**Validates**:
- Multi-field command encoding
- Configuration acknowledgment
- Device configuration report (CFG_REPORT) reception

---

### 3. **Data Streaming Session (STREAM_START)**

```cpp
pc.send_stream_start(1000, 0x000F, loop.now_us());
loop.run_for(10'000'000, 500, [&](uint64_t now_us) {
    link.pump(now_us);
    pc.tick(now_us);
    device.tick(now_us);
});
```

**Purpose**: Start real-time data streaming and collect samples for 10 seconds.

**Parameters**:
- `period_us`: 1000 (1ms sampling period → 1kHz rate)
- `mask`: 0x000F (enable all channels: voltage, current, power, temperature)

**Duration**: 10 seconds (10,000,000 microseconds)

**Tick Interval**: 500 μs (2 kHz simulation rate)

**Validates**:
- Stream start command processing
- Continuous data sample transmission (DATA_SAMPLE messages)
- Timestamp accuracy
- Multi-channel data encoding
- Sustained high-rate communication

**Expected Behavior**:
- Device sends DATA_SAMPLE frames at 1ms intervals
- Each sample contains: timestamp, flags, vbus, current, temperature
- ~10,000 samples collected during the session

---

### 4. **Stream Termination (STREAM_STOP)**

```cpp
pc.send_stream_stop(loop.now_us());
loop.run_for(500'000, 500, [&](uint64_t now_us) {
    link.pump(now_us);
    pc.tick(now_us);
    device.tick(now_us);
});
```

**Purpose**: Stop data streaming and verify clean shutdown.

**Duration**: 500ms grace period for in-flight messages

**Validates**:
- Stream stop command processing
- Clean termination of data flow
- No data samples after stop acknowledgment

---

## Communication Quality Metrics

After the simulation completes, the following error statistics are reported:

```cpp
std::cout << "Summary: crc_fail=" << pc.crc_fail_count()
          << " data_drop=" << pc.data_drop_count()
          << " timeouts=" << pc.timeout_count()
          << " retries=" << pc.retransmit_count() << "\n";
```

### Metrics Explained

| Metric | Description | Expected Value |
|--------|-------------|----------------|
| `crc_fail` | Number of frames with CRC errors | 0 (with fault injection: varies) |
| `data_drop` | Number of data samples lost/discarded | 0 (ideal link) |
| `timeout` | Number of command timeouts | 0 (ideal link) |
| `retries` | Number of retransmissions | 0 (ideal link) |

---

## Fault Injection Configuration

The simulator includes configurable fault injection to test protocol robustness:

```cpp
sim::LinkConfig config;
config.min_chunk = 1;       // Minimum bytes per transmission chunk
config.max_chunk = 16;      // Maximum bytes per transmission chunk
config.min_delay_us = 0;    // Minimum transmission delay
config.max_delay_us = 2000; // Maximum transmission delay (2ms)
config.drop_prob = 0.0;     // Packet drop probability (0-1)
config.flip_prob = 0.0;     // Bit flip probability (0-1)
```

### Default Configuration (Ideal Link)

The default settings simulate a perfect communication link:
- No packet drops (`drop_prob = 0.0`)
- No bit flips (`flip_prob = 0.0`)
- Variable chunking (1-16 bytes) to test frame reassembly
- Variable delay (0-2ms) to test timing tolerance

### Testing with Faults

To test protocol resilience, modify the configuration:

```cpp
config.drop_prob = 0.05;  // 5% packet loss
config.flip_prob = 0.01;  // 1% bit error rate
```

**Expected Results with Faults**:
- CRC failures increase (detected and discarded)
- Timeouts may occur (triggering retransmissions)
- Retransmission counter increases
- Data samples may be lost (data_drop counter)
- **Protocol should remain stable** and recover automatically

---

## Test Coverage Summary

This single integration test covers:

✅ **Protocol Features**:
- Frame encoding/decoding
- CRC16-CCITT-FALSE validation
- Sequence numbering
- Command-response pattern
- Event/data message handling

✅ **Communication Patterns**:
- Request-response (PING, SET_CFG)
- Event notifications (CFG_REPORT)
- Streaming data (DATA_SAMPLE at 1kHz)

✅ **Error Handling**:
- CRC error detection
- Timeout and retransmission
- Packet loss recovery
- Bit error resilience

✅ **Performance**:
- High-rate data streaming (1kHz)
- Large data volume (~10k samples)
- Sustained 10-second operation

---

## Running the Simulator

```bash
# Build
cmake -S pc_sim -B build_pc
cmake --build build_pc --target pc_sim

# Run
./build_pc/pc_sim
```

**Expected Output** (ideal link):
```
CMD send msgid=0x1 seq=0
CMD send msgid=0x10 seq=1
CMD send msgid=0x30 seq=2
DEV CFG_REPORT sent
RSP msgid=0x1 status=0x0
RSP msgid=0x10 status=0x0
RSP msgid=0x30 status=0x0
DATA ts_us=1000000 flags=0x5 vbus=12.0 current=0.5 temp=35.2
[... ~10,000 DATA samples ...]
CMD send msgid=0x31 seq=3
RSP msgid=0x31 status=0x0
Summary: crc_fail=0 data_drop=0 timeouts=0 retries=0
```

---

## Modifying Test Parameters

### Change Sampling Rate

```cpp
// High-speed sampling (10kHz)
pc.send_stream_start(100, 0x000F, loop.now_us());

// Low-speed sampling (1Hz)
pc.send_stream_start(1000000, 0x000F, loop.now_us());
```

### Select Channels

```cpp
// Voltage only
pc.send_stream_start(1000, 0x0001, loop.now_us());

// Voltage + Current
pc.send_stream_start(1000, 0x0003, loop.now_us());

// All channels (voltage, current, power, temperature)
pc.send_stream_start(1000, 0x000F, loop.now_us());
```

### Adjust Test Duration

```cpp
// Short test (1 second)
loop.run_for(1'000'000, 500, [...]);

// Long test (60 seconds)
loop.run_for(60'000'000, 500, [...]);
```

---

## Interpreting Results

### Success Criteria

A successful test run should show:
- All commands receive responses (`RSP msgid=...`)
- No CRC failures
- No timeouts
- No retransmissions
- Data samples arrive continuously

### Failure Indicators

| Symptom | Possible Cause |
|---------|----------------|
| High `crc_fail` | Link noise, incorrect CRC implementation |
| `timeout > 0` | Device not responding, excessive packet loss |
| `retries > 0` | Packet loss, delay exceeding timeout threshold |
| `data_drop > 0` | Device buffer overflow, high packet loss |
| No DATA samples | Stream start failed, device error |
| Crash/hang | Parser bug, infinite loop, memory corruption |

---

## Future Test Enhancements

Potential additions for more comprehensive testing:

- [ ] Multiple start/stop cycles
- [ ] Configuration changes during streaming
- [ ] Stress testing with extreme fault injection (50% packet loss)
- [ ] Timing accuracy verification
- [ ] Buffer overflow testing (very high data rates)
- [ ] Graceful degradation under sustained errors
- [ ] Power cycle recovery simulation
- [ ] Concurrent command handling

---

## Related Documentation

- [INA228 UART Protocol](../protocol/uart_protocol.md) - Protocol specification
- [Time Sync Documentation](../device/time_sync.md) - Time synchronization details
- [README](../../README.md) - Project overview and build instructions
