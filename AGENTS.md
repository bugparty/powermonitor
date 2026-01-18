# AGENTS.md

**IMPORTANT**: This document is used to guide AI Agents in understanding the project structure and documentation.

All documentation in this project must be written in **English**, regardless of the language you communicate with the user in. You may interact with the user in whatever language they prefer (Chinese, English, etc.), but all code comments, documentation files, commit messages, and other written outputs must be in English.

---

## Language Policy (IMPORTANT)

### ⚠️ English Documentation Required

**All documentation, comments, and written outputs must be in ENGLISH.**

This includes but is not limited to:
- Documentation files (`docs/**/*.md`, `README.md`, `device/*.md`)
- Code comments
- Commit messages
- Test descriptions
- Variable and function names (already in English by convention)
- GitHub PR descriptions and comments

**Why?**
- Consistency across the codebase
- Accessibility to international contributors
- Better integration with development tools
- Industry best practice for open-source projects

**Interaction with Users:**
- You may understand and respond to users in any language (Chinese, English, etc.)
- You should translate user requirements accurately
- But all outputs to files must be in English

**Examples:**
- User asks in Chinese: "请帮我修复这个bug" → You can respond in Chinese, but code comments and commit messages must be in English
- User asks in English: "Please add a new feature" → Respond and document in English

---

## ⚠️ Important: Testing Requirements

**You MUST run tests to verify after modifying any code!**

### Running Tests

```bash
# Basic usage: quick test run
./test.sh

# Clean rebuild and run tests
./test.sh --clean

# View detailed test output
./test.sh --verbose

# View help
./test.sh --help
```

### Test Pass Criteria

All test cases must pass (`[PASSED] 28 tests`).

**❌ If tests fail, do NOT commit! Fix the errors first.**

---

## Project Overview

This project is a power monitoring system based on the INA228 power monitoring chip, including:
- **device/**: Embedded device-side code (Raspberry Pi Pico RP2040)
- **docs/**: Protocol specification and technical documentation
- **app/**: PC-side protocol simulator demo
- **pc_sim/**: PC-side protocol simulator and Google Test suite
- **protocol/**: Protocol implementation (shared by PC and device)
- **sim/**: Simulation infrastructure (virtual link, event loop)
- **node/**: Protocol node implementation (PC node, device node, sensor model)

---

## Documentation Index

**You MUST review the following documents before starting any development work:**

| Document Path | Description |
|--------------|-------------|
| `README.md` | **Main project document** - Project overview, build instructions, test instructions, architecture diagram |
| `docs/protocol/uart_protocol.md` | **INA228 UART Communication Protocol Specification** - Defines complete communication protocol between PC and device, including frame format, message types, data format, CRC validation, etc. |
| `docs/pc_sim/simulator_tests.md` | **PC Simulator Test Documentation** - Detailed description of test scenarios, metrics, fault injection configuration |
| `docs/pc_sim/state_machine_tests.md` | **State Machine Test Documentation** - Complete test cases and coverage requirements for protocol parser state machine |
| `docs/naming_convention.md` | Documentation naming convention and code mapping rules |
| `device/README.md` | Hardware information, development environment setup, build and flash guide |
| `docs/device/time_sync.md` | Technical documentation for time synchronization module |

---

## Key Protocol Points (Quick Reference)

> For detailed information, please refer to `docs/protocol/uart_protocol.md`

- **Frame Format**: SOF(0xAA 0x55) + Header + Payload + CRC16
- **CRC Algorithm**: CRC-16/CCITT-FALSE (Poly=0x1021, Init=0xFFFF)
- **Endianness**: Little-Endian
- **20-bit Data**: LE-packed 3-byte format

---

# INA228 Protocol Simulator (PC-side) Implementation Document v0.1

This is a complete implementation specification that can be directly given to agents for development (targeting "single-process event loop + virtual serial link + device-side simulation + PC-side protocol stack"). Written according to your latest protocol (LEN includes MSGID, CRC16-CCITT-FALSE, DATA/EVT sequence space, etc.).

---

## 0. Objective

Implement a **pure software simulation environment** on PC for debugging INA228 protocol PC/Device communication without real serial ports or hardware:

- Single-process, **event loop** driven (can run without multi-threading)
- Provide **Virtual Serial Link** (bidirectional byte stream), supporting packet sticking/fragmentation/delay/loss/bit flip
- Device-side simulation behavior:
  - On receiving `SET_CFG`: reply with `RSP(OK)`, immediately followed by `CFG_REPORT`
  - On receiving `STREAM_START(period_us, channel_mask)`: reply with `RSP(OK)`, then send `DATA_SAMPLE` periodically at `period_us`
  - Voltage/current: generate raw values using **simple waveform** or **random walk** (20-bit packed)
- PC-side protocol stack implementation:
  - Frame building/parsing, CRC validation, parser state machine, command timeout retransmission (recommended)
  - Parse `CFG_REPORT` and save `current_lsb_nA/adcrange`
  - Parse `DATA_SAMPLE`, perform frame loss detection (SEQ), and optionally convert to engineering values for output

---

## 1. Overall Architecture

### 1.1 Module Division

1. `protocol/`
   - `crc16_ccitt_false.{h,cpp}`
   - `frame_builder.{h,cpp}`: Encapsulate frames according to protocol (SOF, VER, TYPE, FLAGS, SEQ, LEN, MSGID, DATA, CRC)
   - `parser.{h,cpp}`: Byte stream parser (state machine), output Frame object (or callback)
   - `unpack.{h,cpp}`: 20-bit LE-packed unpacking/sign extension, engineering value conversion (optional)

2. `sim/`
   - `virtual_link.{h,cpp}`: Virtual bidirectional link, fault injection (fragmentation/sticking/delay/loss/flip)
   - `event_loop.{h,cpp}`: Event loop + timer queue (prefer minimal single-thread implementation)

3. `node/`
   - `pc_node.{h,cpp}`: PC-side protocol behavior (send commands, retransmission, handle CFG/DATA)
   - `device_node.{h,cpp}`: Device-side simulation (process commands, generate CFG_REPORT, periodically send DATA_SAMPLE)
   - `ina228_model.{h,cpp}`: Generate simulated measurement values (waveform/random walk), output raw20

4. `app/`
   - `main.cpp`: Assemble all components, configure link parameters, start event loop, output logs

### 1.2 Data Flow and Control Flow

- PC/Device both send bytes via `VirtualLinkEndpoint`: `endpoint.write(bytes)`
- Event Loop each tick:
  1. Call `link.pump(now)`: Deliver chunks with arrival time due to peer RX buffer
  2. PC/Device respectively execute:
     - `rx = endpoint.read_available()` (non-blocking read all or part)
     - `parser.feed(rx)` → `on_frame(frame)` callback processing
  3. Execute timers (command timeout retransmission, streaming timer send, etc.)

---

## 2. Key Protocol Details (Implementation MUST Comply)

### 2.1 Frame Format (LEN Includes MSGID)

Frame layout:

```powershell
SOF0=0xAA, SOF1=0x55
VER(1) TYPE(1) FLAGS(1) SEQ(1) LEN(2 LE)
MSGID(1) DATA(N-1)
CRC16(2 LE)   // CRC over VER..DATA_end

```

- `LEN = 1 + DataLen` (includes MSGID itself)
- CRC16: CRC16-CCITT-FALSE (poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0)

### 2.2 CRC Fail Behavior

- **Any frame CRC fail: silently discard** (do not reply RSP(ERR_CRC))
- Command timeout → PC retransmission resolves

### 2.3 SEQ Rules (Sequence Space Isolation)

- CMD: PC maintains `cmd_seq` auto-increment
- RSP: Device echoes corresponding CMD's SEQ
- DATA/EVT: Device maintains `data_seq` auto-increment (mod 256), used by PC for frame loss detection

---

## 3. Virtual Link (Serial Simulation Layer) Specification

### 3.1 Endpoint API

- `write(ByteVec bytes)`: Write to send queue (enter link simulation)
- `read(size_t max_bytes)`: Read arrived RX bytes
- `available()`: Number of RX bytes

### 3.2 LinkConfig (Fault Injection Parameters)

Each direction has independent configuration (PC→DEV / DEV→PC):

- `min_chunk, max_chunk`: Fragmentation/sticking simulation (one write split into multiple segments)
- `min_delay_us, max_delay_us`: Delivery delay range per chunk segment
- `drop_prob`: Per-chunk drop probability
- `flip_prob`: Per-byte flip probability (randomly flip 1 bit)

Recommended defaults (for debugging parser):

- `min_chunk=1, max_chunk=16`
- `min_delay_us=0, max_delay_us=2000`
- `drop_prob=0.0`
- `flip_prob=0.0`

---

## 4. Parser State Machine Implementation Requirements

### 4.1 Input and Output

- Input: Arbitrary segmented incoming bytes (may be partial frames/multiple frames/noise)
- Output: Validated Frame objects:
  - `ver,type,flags,seq,len,msgid,data[]`

### 4.2 Required Robustness

- Must handle packet sticking/fragmentation
- Must resynchronize from noise (scan for `AA 55`)
- `LEN` must perform upper bound check: `LEN==0` or `LEN>MAX_LEN` → directly discard and resync
  - `MAX_LEN` recommended 1024 (or 256, per project requirements)

---

## 5. Device-side Simulation Behavior (MUST Implement)

### 5.1 Maintained State

- `config_reg`, `adc_config_reg`, `shunt_cal_reg`, `shunt_tempco` (optional)
- `current_lsb_nA` (set by SET_CFG or fixed)
- `flags`:
  - `streaming_on`
  - `cal_valid` (shunt_cal_reg != 0 considered true)
  - `adcrange` (from config or fixed)
- streaming:
  - `stream_period_us`
  - `stream_mask`
  - `next_sample_due_us`
- Device data sequence: `data_seq`

### 5.2 Command Processing

#### PING (0x01)

- Reply with `RSP(OK)` (may include firmware version info, optional)

#### SET_CFG (0x10)

- Parse payload (per protocol definition: config_reg/adc_config_reg/shunt_cal/shunt_tempco)
- Update internal state
- Immediately reply: `RSP(orig=0x10, status=OK)`
- Immediately send: `EVT CFG_REPORT(0x91)`

#### GET_CFG (0x11)

- Reply with `RSP(OK)` (no additional data)
- Immediately send: `EVT CFG_REPORT(0x91)`

#### STREAM_START (0x30)

- Parse: `period_us, channel_mask`
- Update: `stream_period_us`, `stream_mask`, `streaming_on=true`
- Reply with `RSP(OK)`
- Set: `next_sample_due_us = now_us + stream_period_us`

#### STREAM_STOP (0x31)

- `streaming_on=false`
- Reply with `RSP(OK)`

---

## 6. DATA_SAMPLE Generation (Waveform/Random Walk)

### 6.1 Output Format (raw 20-bit)

DATA_SAMPLE payload (fixed 16 bytes):

- `timestamp_us (u32 LE)`: Accumulate after clearing at STREAM_START execution moment (wraparound allowed)
- `flags (u8)`: CNVRF/ALERT/CAL_VALID/OVF etc. (simplified in simulation)
- `vbus20[3]`: u20 LE-packed (register>>4 20-bit value)
- `vshunt20[3]`: s20 LE-packed
- `current20[3]`: s20 LE-packed
- `dietemp16 (i16 LE)`

### 6.2 Waveform Recommendation (Default Implementation)

- `vbus_V`: 12.0V + 0.1V*sin(2π*0.5Hz*t) + small noise
- `current_A`: 0.5A + 0.2A*sin(2π*0.8Hz*t + phase) + small noise
- `temp_C`: 35C + random walk (small step size)

### 6.3 Engineering Value → raw20 Conversion (Device-side Simulation)

- VBUS raw20:
  - LSB = 195.3125 µV/LSB
  - `raw = clamp(round(vbus_V / 195.3125e-6), 0..0xFFFFF)`
- CURRENT raw20 (depends on current_lsb_nA):
  - `CURRENT_A = signed_raw * (current_lsb_nA * 1e-9)`
  - `signed_raw = clamp(round(current_A / (current_lsb_nA*1e-9)), -524288..524287)`
- VSHUNT raw20 (depends on adcrange):
  - adcrange=0: 312.5nV/LSB
  - adcrange=1: 78.125nV/LSB
  - `signed_raw = clamp(round(vshunt_V / lsb), -524288..524287)`
  - vshunt_V can be obtained from `current_A * rshunt` (rshunt can be fixed, e.g., 10mΩ), or independent waveform
- DIETEMP raw16:
  - LSB = 7.8125 m°C/LSB
  - `raw16 = clamp(round(temp_C / 0.0078125), -32768..32767)`

### 6.4 LE-packed 20-bit Encoding

Implementation for agents:

- Input: `uint32_t u20` (0..0xFFFFF) or `int32_t s20` (range [-524288, 524287])
- Output:
  - `buf[0] = raw & 0xFF`
  - `buf[1] = (raw >> 8) & 0xFF`
  - `buf[2] = (raw >> 16) & 0x0F` (only low 4 bits valid)

> For signed numbers: First convert s20 to 20-bit two's complement representation (`raw = (uint32_t)(s20 & 0xFFFFF)`)

---

## 7. PC-side Behavior (Recommended Implementation)

### 7.1 PC Protocol Layer Responsibilities

- Maintain cmd_seq, auto-increment when sending CMD
- Maintain outstanding command table (seq→(msgid, deadline, retries, bytes))
- On receiving RSP:
  - Verify `orig_msgid` matches outstanding command
  - Cancel timeout
- Timeout retransmission:
  - Retransmit with same SEQ, maximum 3 times
- On receiving CFG_REPORT:
  - Save: `current_lsb_nA`, `adcrange`, `stream_period_us`, `stream_mask`
- On receiving DATA_SAMPLE:
  - Record DATA SEQ frame loss statistics
  - Optionally convert to engineering value and print

### 7.2 Recommended Demo Scenario (main)

1. PC sends `PING`
2. PC sends `SET_CFG` (provide shunt_cal corresponding to current_lsb_nA)
3. PC sends `STREAM_START(period_us=1000, mask=0x000F)`
4. Run for 10 seconds: Print frame loss rate, average voltage/current, CRC fail count (if flip enabled)
5. PC sends `STREAM_STOP`

---

## 8. Logging and Observability Requirements

Must output the following logs/statistics:

- Send/receive count per MSGID type
- CRC fail count (both parser ends)
- DATA frame loss count (PC)
- Command timeout and retransmission count (PC)
- Optional: hex dump (switch controlled, avoid screen flooding)

---

## 9. Development Workflow (Agents MUST Comply)

### 9.1 Before Modifying Code

1. **Read relevant documentation**: Review `docs/protocol/uart_protocol.md` and `README.md`
2. **Understand tests**: Review `docs/pc_sim/simulator_tests.md` to understand test coverage
3. **Run current tests**: `./test.sh` to ensure clean starting point

### 9.2 While Modifying Code

1. **Incremental development**: Small steps, frequent testing
2. **Follow protocol specification**: Strictly implement per `docs/protocol/uart_protocol.md`
3. **Maintain compilation**: Ensure code compiles after each modification

### 9.3 After Modifying Code (Mandatory Requirement)

**Must run and pass tests before commit!**

```bash
# Recommended: Clean rebuild and test
./test.sh --clean

# Quick test (incremental build)
./test.sh
```

**Acceptance Criteria**:
- ✅ All 28 tests pass (6 functional + 22 state machine)
- ✅ No compilation warnings
- ✅ No runtime crashes
- ✅ Output logs reasonable (no abnormal error messages)

### 9.4 When Tests Fail

1. **View detailed output**: `./test.sh --verbose`
2. **Locate failed test**: Find test marked with `[  FAILED  ]`
3. **Analyze failure cause**:
   - CRC error? Check CRC calculation and frame format
   - Timeout? Check event loop and timers
   - Data mismatch? Check protocol implementation and data encoding
4. **Retest after fix**: `./test.sh`

### 9.5 Adding New Features

1. **Write test first** (TDD approach):
   - Add new `TEST_F` case in `pc_sim/*.cpp`
   - Run test to confirm failure (red)
2. **Implement feature**: Modify protocol implementation
3. **Run test**: `./test.sh`, confirm pass (green)
4. **Refactor**: Optimize code while keeping tests passing

### 9.6 Test Documentation and Test Code Synchronization Principle

**Important principle: Test documentation (`docs/pc_sim/state_machine_tests.md`) and test code (`pc_sim/*.cpp`) must be kept in sync!**

#### 9.6.1 Document Update → Test Code Must Update

If test documentation needs update (e.g., adding test cases, modifying test steps, updating expected results):

1. **Immediately update corresponding test code** (`pc_sim/state_machine_test.cpp` or `pc_sim/functional_test.cpp`)
2. **Run tests to verify update is correct**
3. **Submit both document and test code modifications in PR**

  **Prohibited to update document only without updating test code!**

#### 9.6.2 Test Code Update → Document Should Update

If test implementation code is modified:

1. **Check if test documentation needs synchronized update**
2. If test logic, steps, or coverage is modified, must reflect in documentation
3. **Keep document description consistent with code implementation**

#### 9.6.3 Exception: Document Error

Only when **document itself has errors** is document update allowed first:

1. **Discover and confirm document error**: Inconsistent with code implementation, unclear description, logical error
2. **Report document error to user**: Clearly identify error content, suggest modification plan
3. **After obtaining explicit user consent**:
   - Update document to correct error
   - Ensure test code remains consistent with revised document
   - Run tests to verify

  **Not allowed to modify document without user consent!**

#### 9.6.4 Synchronization Checklist

Use this checklist for every modification involving test-related code:

- [ ] If test logic modified, is test documentation updated?
- [ ] If test documentation modified, is test code implementation updated?
- [ ] Are document and code descriptions consistent?
- [ ] Do all tests pass? (`./test.sh`)
- [ ] Does PR contain dual modifications of document and code?

---

## 10. Deliverables and Acceptance Criteria

### 10.1 Deliverables

- Compilable and runnable C++17/20 project (CMake)
- Test suite: One-click run `./test.sh` and all pass (28/28 tests passed)
- Run examples: `./build_pc/pc_sim` or `./build/pc_sim/pc_sim` automatically complete demo scenario
- Complete documentation (README, protocol documents, test documents)

### 10.2 Acceptance Criteria

**Mandatory: All 28 Google Test test cases must pass!**

Running `./test.sh --clean` should display:
```
[==========] Running 28 tests from 2 test suites.
[  PASSED  ] 28 tests.
[SUCCESS] All tests passed! ✓
```

**Specific Test Scenario Acceptance**:

- With `drop_prob=0, flip_prob=0`:
  - PC can stably receive CFG_REPORT
  - DATA_SAMPLE frequency close to period_us (scheduling error allowed)
  - No parsing errors, no frame loss (or minimal, depending on event loop tick)
- With `min_chunk=1,max_chunk=8`:
  - Parser can still correctly cut frames (no crash, no hang)
- With `flip_prob>0`:
  - CRC fail is counted but won't cause parser desynchronization (will recover)
  - Command timeout retransmission can restore communication

---
