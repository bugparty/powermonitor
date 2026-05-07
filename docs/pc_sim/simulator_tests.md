# PC Simulator Tests

Related code:
- `pc_sim/functional_test.cpp` — end-to-end functional tests (Google Test)
- `pc_sim/state_machine_test.cpp` — parser state machine unit tests
- `node/pc_node.cpp` / `node/device_node.cpp` — nodes under test

This document describes the test scenarios in `pc_sim/functional_test.cpp` and the metrics exposed by `PCNode`.

---

## Test Fixtures

### `PowerMonitorTest`

Full PC + Device pair connected over a `VirtualLink`. Default link is clean (no drops, no bit flips, 0–2 ms delay, 1–16 byte chunks). Most happy-path and fault-injection tests use this fixture.

### `CommandTimeoutTest`

PC only — no `DeviceNode`. PC→device direction has `drop_prob = 1.0`, so commands never reach the device and no RSP is ever sent. Used to exercise the retransmit/timeout path in isolation.

### `FaultInjectionTest`

PC only — no `DeviceNode`. Clean link in both directions. Provides an `InjectFrame()` helper to inject crafted frames from device→PC direction. Used to test orphan RSP and error RSP detection without interference from a real device.

---

## Test Cases

### Happy-path tests

#### `PingCommand`
Sends a single PING and runs for 100 ms.

**Asserts**: `crc_fail = 0`, `timeout = 0`, `orphan_rsp = 0`.

---

#### `SetConfiguration`
Sends `SET_CFG` with `shunt_cal = 0x1000` and runs for 100 ms.

**Asserts**: `crc_fail = 0`, `timeout = 0`.

---

#### `StreamStartStop`
Sends `STREAM_START` (1 kHz), runs 1 s, then `STREAM_STOP`.

**Asserts**: `crc_fail = 0`, `timeout = 0`.

---

#### `CompleteDataStreamingScenario`
Full workflow: PING → SET_CFG → STREAM_START → 10 s streaming → STREAM_STOP.

**Asserts** (all must be 0 on a clean link):
- `crc_fail_count`
- `data_drop_count`
- `timeout_count`
- `retransmit_count`
- `orphan_rsp_count`
- `error_rsp_count`
- `truncated_data_count`

---

#### `TimeSynchronizationSequence`
Sends `TIME_SYNC`, waits for RSP and the automatic `TIME_ADJUST` it triggers.
Then sends `TIME_SET`.

**Asserts**: no timeouts or CRC errors; `rx_count(TIME_SYNC) = 1`, `rx_count(TIME_ADJUST) = 1`, `rx_count(TIME_SET) = 1`.

---

### CFG_REPORT content verification

#### `CfgReportValuesParsedAfterGetCfg`
Starts streaming at 1 kHz with mask `0x000F`, then sends `GET_CFG`.
Verifies that the `CFG_REPORT` sent by the device is correctly parsed by `PCNode`.

**Asserts**:
- `stream_period_us_cfg() = 1000`
- `stream_mask_cfg() = 0x000F`
- `current_lsb_nA() = 1000`
- `timeout_count = 0`

---

### Data sequence number tests

#### `DataSeqWraparoundNoFalseDrops`
Streams at 1 kHz for 350 ms (~350 DATA frames) without any `SET_CFG` during streaming.

**Rationale**: `data_seq_` in `DeviceNode` is a `uint8_t` shared by EVT and DATA frames.
After the initial `CFG_REPORT` (seq=0) and `TEXT_REPORT` (seq=1), DATA frames start at seq=2.
After 254 DATA frames the counter wraps: 255 → 0. `PCNode` uses
`static_cast<uint8_t>(last_data_seq_ + 1)` for the expected value, so the wraparound is
handled correctly.

**Asserts**: `data_drop_count = 0` (no spurious gaps from the uint8 wraparound).

> **Note**: Sending `SET_CFG` *during* streaming causes the device to emit an extra
> `CFG_REPORT` (which increments `data_seq_`), making the next DATA frame seq appear to
> skip a number. `data_drop_count` would then be non-zero — this is expected behaviour and
> is NOT a bug in `PCNode`.

---

### Fault-injection tests

#### `PacketDropsCauseDetectableErrors`
Applies 5% chunk-level drop probability on both link directions. Streams at 1 kHz for 2 s.

**Rationale**: A DATA_SAMPLE frame is ~48 bytes → ~3 chunks at `max_chunk = 16`. The probability
that at least one chunk is dropped is `1 - 0.95³ ≈ 14%`, so across 2000 frames ~280 are
expected to produce CRC errors.

**Asserts**: `crc_fail_count > 0` — errors must be *detected*, not silently swallowed.

---

#### `BitFlipsCauseDetectableCrcErrors`
Applies 1% per-bit flip probability on both link directions. Streams at 1 kHz for 2 s.

**Rationale**: A 48-byte frame has 384 bits. `P(at least one flip) = 1 - 0.99³⁸⁴ ≈ 98%`,
so virtually every frame is corrupted.

**Asserts**: `crc_fail_count > 0`.

---

### Orphan RSP / Error RSP tests

These tests use a dedicated `FaultInjectionTest` fixture (PC only, no device) to inject crafted RSP frames directly into the PC's parser.

#### `OrphanRspDetected`
Injects a RSP frame with `seq = 0x42` (not in the `pending_` map). Verifies that `orphan_rsp_count` is incremented.

**Asserts**: `orphan_rsp_count = 1`, `error_rsp_count = 0`.

---

#### `ErrorRspDetected`
Sends a PING command (creates pending entry), then injects a RSP with matching seq but `status = ERR_HW (0x05)`.

**Asserts**: `orphan_rsp_count = 0`, `error_rsp_count = 1`, `timeout_count = 0` (command completed by error RSP).

---

#### `OrphanRspDetectedOnMsgidMismatch`
Sends a PING command, then injects a RSP with matching seq but `orig_msgid = SET_CFG (0x10)` instead of PING.

**Asserts**: `orphan_rsp_count = 1`, `error_rsp_count = 0`.

---

#### `MultipleOrphanRspsCounted`
Injects 5 orphan RSPs with different seq values. Verifies each is counted.

**Asserts**: `orphan_rsp_count = 5`.

---

### Timeout / retransmit tests

#### `CommandRetransmitsAndTimesOut`
Uses `CommandTimeoutTest` fixture (100% drop). Sends a single PING and runs for 900 ms.

**Timing** (`kCmdTimeoutUs = 200 ms`, `kMaxRetries = 3`):

| Time | Event |
|------|-------|
| t = 0 ms | PING sent (retries = 0) |
| t = 200 ms | Deadline hit → retransmit (retries = 1) |
| t = 400 ms | Deadline hit → retransmit (retries = 2) |
| t = 600 ms | Deadline hit → retransmit (retries = 3) |
| t = 800 ms | Deadline hit, retries ≥ 3 → timeout recorded |

**Asserts**: `retransmit_count = 3`, `timeout_count = 1`.

---

#### `MultipleCommandsAllTimeout`
Uses `CommandTimeoutTest` fixture. Sends PING and GET_CFG simultaneously, runs for 900 ms.

**Asserts**: `timeout_count = 2`, `retransmit_count = 6` (3 per command).

---

## PCNode Metrics Reference

| Accessor | Incremented when |
|----------|-----------------|
| `crc_fail_count()` | Parser rejects a frame due to CRC mismatch |
| `data_drop_count()` | DATA_SAMPLE `frame.seq` ≠ `last_data_seq_ + 1` (uint8) |
| `timeout_count()` | Pending command exhausts `kMaxRetries` retransmits |
| `retransmit_count()` | Pending command deadline hits and a retry is sent |
| `orphan_rsp_count()` | RSP arrives with no matching pending command, or `orig_msgid` mismatches the pending entry |
| `error_rsp_count()` | RSP received with `status ≠ 0x00` (non-OK) |
| `truncated_data_count()` | DATA_SAMPLE payload is shorter than the minimum 41 bytes |
| `get_rx_count(msgid)` | Per-MSGID receive counter (all frame types) |

**Config accessors** (updated from the last parsed `CFG_REPORT`):
- `current_lsb_nA()` — current LSB in nA
- `stream_period_us_cfg()` — streaming period in µs
- `stream_mask_cfg()` — active channel mask

---

## DeviceNode DATA_SAMPLE Frame Layout

`DeviceNode::send_data_sample()` produces a 41-byte payload:

| Offset | Size | Field |
|--------|------|-------|
| 0–7 | 8 B | `timestamp_unix_us` (monotonic + epoch_offset) |
| 8–15 | 8 B | `timestamp_us` (relative to stream start, 64-bit) |
| 16 | 1 B | flags |
| 17–19 | 3 B | vbus20 (20-bit LE-packed) |
| 20–22 | 3 B | vshunt20 (20-bit signed LE-packed) |
| 23–25 | 3 B | current20 (20-bit signed LE-packed) |
| 26–28 | 3 B | power24 (not modelled — zeros) |
| 29–30 | 2 B | temp16 (signed, raw INA228 die temp) |
| 31–35 | 5 B | energy40 (not modelled — zeros) |
| 36–40 | 5 B | charge40 (not modelled — zeros) |

`PCNode::handle_data_sample()` requires `frame.data.size() >= 41`; frames shorter than this
increment `truncated_data_count` and are discarded.

---

## Fault Injection Configuration

```cpp
sim::LinkConfig config;
config.min_chunk    = 1;     // bytes per delivery chunk (min)
config.max_chunk    = 16;    // bytes per delivery chunk (max)
config.min_delay_us = 0;     // per-chunk delivery delay (min)
config.max_delay_us = 2000;  // per-chunk delivery delay (max)
config.drop_prob    = 0.0;   // probability a chunk is dropped entirely
config.flip_prob    = 0.0;   // probability each bit is flipped
```

Drop and flip are applied independently per chunk, per direction.

---

## Coverage Summary

| Area | Status |
|------|--------|
| Frame encode/decode + CRC | ✅ (state_machine_test.cpp) |
| Command → RSP round-trip (PING, SET_CFG, GET_CFG, STREAM_START/STOP, TIME_SYNC/SET) | ✅ |
| CFG_REPORT content parsing | ✅ |
| DATA_SAMPLE streaming (sustained 10 s, 1 kHz) | ✅ |
| DATA seq uint8 wraparound (255 → 0) | ✅ |
| Command retransmit logic (exactly kMaxRetries retransmits) | ✅ |
| Command timeout after max retries (exactly 1 timeout per command) | ✅ |
| Multiple simultaneous commands each timing out independently | ✅ |
| CRC error detection with 5% chunk drop | ✅ |
| CRC error detection with 1% bit flip | ✅ |
| Error counters for silent-drop paths (`orphan_rsp`, `error_rsp`, `truncated_data`) | ✅ all covered by fault-injection tests |
| SET_CFG during streaming (seq gap side-effect) | ⬜ not yet covered |
| High packet loss (≥ 50%) | ⬜ not yet covered |

---

## Running Tests

```bash
# Build + run all tests
pwsh workflow.ps1

# Run with verbose output
pwsh workflow.ps1 -Verbose

# Run a single test
./build_linux/bin/pc_sim_test --gtest_filter=PowerMonitorTest.DataSeqWraparoundNoFalseDrops
```

All 61 tests must pass before committing.

---

## Related Documentation

- [UART Protocol](../protocol/uart_protocol.md) — frame format, MSGIDs, CRC
- [State Machine Tests](state_machine_tests.md) — parser state machine unit tests
- [Time Sync](../device/time_sync.md) — time synchronisation algorithm
