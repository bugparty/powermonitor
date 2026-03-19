# INA228 Protocol State Machine Test Specification

## Overview

Related code:
- `pc_sim/state_machine_test.cpp`
- `protocol/parser.cpp`
- `protocol/parser.h`

This document defines comprehensive test cases for the PC-side protocol parser state machine defined in `docs/protocol/uart_protocol.md`. Each state must be covered at least 2 times to ensure robustness.


## State Machine States

Based on the protocol specification, the parser has 6 states:

1. **WAIT_SOF0** - Waiting for first start byte (0xAA)
2. **WAIT_SOF1** - Waiting for second start byte (0x55)
3. **READ_HEADER** - Reading 6-byte header (VER, TYPE, FLAGS, SEQ, LEN_L, LEN_H)
4. **READ_PAYLOAD** - Reading payload (MSGID + DATA)
5. **VERIFY_CRC** - Verifying CRC16 checksum
6. **RESYNC** - Attempting to resynchronize after CRC failure

## Test Cases

### Category 1: WAIT_SOF0 State Coverage (Minimum 2 tests)

#### Test 1.1: Normal SOF0 Reception
**Objective**: Verify transition from WAIT_SOF0 to WAIT_SOF1 on receiving 0xAA

**Test Steps**:
1. Initialize parser in WAIT_SOF0 state
2. Send byte 0xAA
3. Send valid SOF1 byte (0x55) to complete the frame start sequence
4. Verify parser transitions to READ_HEADER state

**Expected Result**: Parser successfully finds complete SOF sequence and enters READ_HEADER state

**State Coverage**: WAIT_SOF0 → WAIT_SOF1 (1st visit)

---

#### Test 1.1a: Invalid SOF1 Handling (Regression Test)
**Objective**: Document current test behavior for SOF0 followed by invalid SOF1

**Test Steps**:
1. Initialize parser in WAIT_SOF0 state
2. Send byte 0xAA (enter WAIT_SOF1)
3. Send invalid byte (0x00, not 0x55)
4. Verify parser resets to WAIT_SOF0

**Expected Result**: Parser returns to WAIT_SOF0 state, treating invalid SOF1 as reset condition

**Note**: This test documents the error recovery behavior when SOF1 is invalid. After Test 1.1 fix, both tests should pass.

**State Coverage**: WAIT_SOF0 → WAIT_SOF1 → WAIT_SOF0

---

#### Test 1.2: Garbage Data Rejection
**Objective**: Verify parser stays in WAIT_SOF0 when receiving non-0xAA bytes

**Test Steps**:
1. Initialize parser in WAIT_SOF0 state
2. Send multiple garbage bytes (0x00, 0xFF, 0x12, 0x34)
3. Verify parser remains in WAIT_SOF0 state after each byte

**Expected Result**: Parser stays in WAIT_SOF0 state, drops all garbage bytes

**State Coverage**: WAIT_SOF0 → WAIT_SOF0 (2nd visit)

---

#### Test 1.3: Recovery from Invalid Frame
**Objective**: Verify parser returns to WAIT_SOF0 after processing invalid frame

**Test Steps**:
1. Send complete frame with invalid CRC (triggers RESYNC → WAIT_SOF0)
2. Verify parser is in WAIT_SOF0 state
3. Send valid frame starting with 0xAA
4. Verify frame is parsed correctly

**Expected Result**: Parser successfully recovers and processes next valid frame

**State Coverage**: WAIT_SOF0 (3rd visit - bonus coverage)

---

### Category 2: WAIT_SOF1 State Coverage (Minimum 2 tests)

#### Test 2.1: Normal SOF1 Reception
**Objective**: Verify transition from WAIT_SOF1 to READ_HEADER on receiving 0x55

**Test Steps**:
1. Initialize parser in WAIT_SOF0 state
2. Send 0xAA (enter WAIT_SOF1)
3. Send 0x55
4. Verify parser transitions to READ_HEADER state

**Expected Result**: Parser enters READ_HEADER state

**State Coverage**: WAIT_SOF1 → READ_HEADER (1st visit)

---

#### Test 2.2: Invalid SOF1 Reset
**Objective**: Verify parser resets to WAIT_SOF0 on receiving non-0x55, non-0xAA byte

**Test Steps**:
1. Send 0xAA (enter WAIT_SOF1)
2. Send 0x12 (invalid SOF1)
3. Verify parser returns to WAIT_SOF0 state

**Expected Result**: Parser resets to WAIT_SOF0

**State Coverage**: WAIT_SOF1 → WAIT_SOF0 (2nd visit)

---

#### Test 2.3: SOF1 Self-Recovery (0xAA Stay)
**Objective**: Verify parser stays in WAIT_SOF1 when receiving 0xAA (treat as new SOF0)

**Test Steps**:
1. Send 0xAA (enter WAIT_SOF1)
2. Send 0xAA again (should stay in WAIT_SOF1, treating it as new SOF0)
3. Send 0x55 (complete SOF sequence)
4. Verify parser enters READ_HEADER state

**Expected Result**: Parser stays in WAIT_SOF1 on second 0xAA, then proceeds normally

**State Coverage**: WAIT_SOF1 → WAIT_SOF1 (3rd visit - bonus coverage)

---

### Category 3: READ_HEADER State Coverage (Minimum 2 tests)

#### Test 3.1: Complete Header Reception
**Objective**: Verify successful reading of 6-byte header and transition to READ_PAYLOAD

**Test Steps**:
1. Send complete valid frame header:
   - SOF: 0xAA 0x55
   - VER: 0x01
   - TYPE: 0x02 (RSP)
   - FLAGS: 0x00
   - SEQ: 0x01
   - LEN_L: 0x03 (3 bytes payload: MSGID + 2 data bytes)
   - LEN_H: 0x00
2. Verify parser transitions to READ_PAYLOAD state
3. Verify LEN is correctly parsed as 3

**Expected Result**: Parser enters READ_PAYLOAD state, expecting 3+2=5 more bytes

**State Coverage**: READ_HEADER → READ_PAYLOAD (1st visit)

---

#### Test 3.2: Header Timeout/Error
**Objective**: Verify parser returns to WAIT_SOF0 on incomplete header reception

**Test Steps**:
1. Send SOF: 0xAA 0x55
2. Send only 3 header bytes (VER, TYPE, FLAGS)
3. Trigger timeout or send next frame's SOF
4. Verify parser resets to WAIT_SOF0

**Expected Result**: Parser returns to WAIT_SOF0 on header error

**State Coverage**: READ_HEADER → WAIT_SOF0 (2nd visit)

---

#### Test 3.3: Multiple Valid Headers
**Objective**: Exercise READ_HEADER state multiple times with different frame types

**Test Steps**:
1. Send complete CMD frame (TYPE=0x01)
2. Send complete RSP frame (TYPE=0x02)
3. Send complete DATA frame (TYPE=0x03)
4. Verify each frame header is parsed correctly

**Expected Result**: All frame types successfully parsed

**State Coverage**: READ_HEADER (bonus coverage)

---

### Category 4: READ_PAYLOAD State Coverage (Minimum 2 tests)

#### Test 4.1: Complete Payload Reception
**Objective**: Verify successful reading of complete payload and transition to VERIFY_CRC

**Test Steps**:
1. Send valid frame header with LEN=3
2. Send complete payload: MSGID (0x01) + 2 data bytes (0x00, 0x00)
3. Send valid CRC16 (2 bytes)
4. Verify parser transitions to VERIFY_CRC state

**Expected Result**: Parser enters VERIFY_CRC state with complete payload

**State Coverage**: READ_PAYLOAD → VERIFY_CRC (1st visit)

---

#### Test 4.2: Payload Timeout
**Objective**: Verify parser returns to WAIT_SOF0 on incomplete payload

**Test Steps**:
1. Send valid frame header with LEN=10
2. Send only 5 payload bytes (incomplete)
3. Trigger timeout
4. Verify parser returns to WAIT_SOF0

**Expected Result**: Parser resets to WAIT_SOF0 on payload timeout

**State Coverage**: READ_PAYLOAD → WAIT_SOF0 (2nd visit)

---

#### Test 4.3: Variable Length Payloads
**Objective**: Test READ_PAYLOAD with different payload sizes

**Test Steps**:
1. Send frame with LEN=1 (MSGID only)
2. Send frame with LEN=10 (MSGID + 9 data bytes)
3. Send frame with LEN=17 (DATA_SAMPLE frame: MSGID + 16 bytes)
4. Verify all payloads read correctly

**Expected Result**: All payload sizes handled correctly

**State Coverage**: READ_PAYLOAD (bonus coverage)

---

### Category 5: VERIFY_CRC State Coverage (Minimum 2 tests)

#### Test 5.1: Valid CRC - Frame Dispatch
**Objective**: Verify correct CRC validation and successful frame dispatch

**Test Steps**:
1. Send complete valid PING frame:
   - SOF: 0xAA 0x55
   - VER: 0x01
   - TYPE: 0x01 (CMD)
   - FLAGS: 0x01 (ACK_REQ)
   - SEQ: 0x05
   - LEN: 0x01 0x00 (1 byte payload)
   - MSGID: 0x01 (PING)
   - CRC: Correct CRC16
2. Verify CRC validation passes
3. Verify frame is dispatched to application layer
4. Verify parser returns to WAIT_SOF0 for next frame

**Expected Result**: Frame successfully validated and dispatched

**State Coverage**: VERIFY_CRC → WAIT_SOF0 (1st visit)

---

#### Test 5.2: Invalid CRC - Enter RESYNC
**Objective**: Verify CRC failure triggers RESYNC state

**Test Steps**:
1. Send complete frame with intentionally corrupted CRC:
   - Valid header and payload
   - CRC: 0xFF 0xFF (incorrect)
2. Verify CRC validation fails
3. Verify parser enters RESYNC state

**Expected Result**: Parser enters RESYNC state due to CRC failure

**State Coverage**: VERIFY_CRC → RESYNC (2nd visit)

---

#### Test 5.3: Multiple Valid Frames in Sequence
**Objective**: Test VERIFY_CRC with consecutive valid frames

**Test Steps**:
1. Send 5 consecutive valid frames with correct CRCs
2. Verify each frame passes CRC validation
3. Verify all frames are dispatched correctly

**Expected Result**: All frames validated and dispatched successfully

**State Coverage**: VERIFY_CRC → WAIT_SOF0 (bonus coverage)

---

### Category 6: RESYNC State Coverage (Minimum 2 tests)

#### Test 6.1: Successful Resync - Find AA 55 in Buffer
**Objective**: Verify RESYNC can find valid SOF sequence in remaining buffer

**Test Steps**:
1. Send frame with corrupted CRC (enter RESYNC)
2. Ensure remaining buffer contains valid AA 55 sequence followed by valid frame
3. Verify parser finds AA 55 and transitions to READ_HEADER
4. Verify next frame is parsed correctly

**Expected Result**: Parser successfully resynchronizes and processes next frame

**State Coverage**: RESYNC → READ_HEADER (1st visit)

---

#### Test 6.2: Failed Resync - No AA 55 Found
**Objective**: Verify RESYNC returns to WAIT_SOF0 when no SOF found in buffer

**Test Steps**:
1. Send frame with corrupted CRC (enter RESYNC)
2. Ensure remaining buffer contains only garbage data (no AA 55)
3. Verify parser exhausts buffer search
4. Verify parser returns to WAIT_SOF0 state

**Expected Result**: Parser returns to WAIT_SOF0 after failed resync

**State Coverage**: RESYNC → WAIT_SOF0 (2nd visit)

---

#### Test 6.3: Partial SOF in RESYNC
**Objective**: Test RESYNC behavior with partial SOF patterns

**Test Steps**:
1. Send frame with corrupted CRC
2. Buffer contains: [garbage] 0xAA [garbage] 0xAA 0x55 [valid frame]
3. Verify parser skips false SOF (lone 0xAA) and finds real AA 55
4. Verify next frame is parsed correctly

**Expected Result**: Parser finds correct SOF sequence and recovers

**State Coverage**: RESYNC (bonus coverage)

---

## Integration Tests (Complex State Sequences)

### Test 7.1: Streaming Data with Packet Loss
**Objective**: Test state machine with high-frequency DATA frames and packet drops

**Test Steps**:
1. Simulate STREAM_START command
2. Send 100 DATA_SAMPLE frames
3. Randomly drop 5 frames (incomplete data)
4. Insert 2 frames with corrupted CRC
5. Verify parser recovers and continues processing valid frames

**Expected States Visited**: All states multiple times

---

### Test 7.2: Communication with Mixed Frame Types
**Objective**: Test realistic communication scenario with all message types

**Test Steps**:
1. Send PING command (CMD frame)
2. Receive PING response (RSP frame)
3. Send SET_CFG command
4. Receive RSP + CFG_REPORT (EVT frame)
5. Send STREAM_START
6. Receive continuous DATA frames
7. Send STREAM_STOP
8. Insert garbage data and corrupted frames throughout

**Expected Result**: All frames processed correctly with proper state transitions

**State Coverage**: Complete state machine coverage

---

### Test 7.3: Stress Test - Rapid State Changes
**Objective**: Test parser robustness under rapid state transitions

**Test Steps**:
1. Rapidly send alternating valid and invalid frames
2. Inject garbage bytes between frames
3. Send incomplete frames
4. Send frames with various CRC errors
5. Monitor parser state and verify no crashes or deadlocks

**Expected Result**: Parser remains stable and processes valid frames correctly

**State Coverage**: All states under stress conditions

---

## Test Implementation Notes

### Test Framework
- Use Google Test (gtest) framework
- Organize tests into test suites by category
- Use parameterized tests where applicable

### Mock Serial Interface
- Implement byte-by-byte feed mechanism
- Support timeout simulation
- Track parser state transitions

### Verification Points
For each test, verify:
1. Current parser state
2. Buffer contents
3. Parsed frame data (if applicable)
4. Error counters and statistics
5. State transition sequence

### Test Data Generation
- Use helper functions to generate valid frames
- CRC calculation utility for generating both valid and invalid CRCs
- Frame corruption utilities for error injection

### Coverage Metrics
- **State Coverage**: Each state visited at least 2 times ✓
- **Transition Coverage**: Each state transition exercised at least once
- **Error Path Coverage**: All error conditions tested

## Test Execution Plan

### Phase 1: Unit Tests (Individual State Tests)
Run Categories 1-6 tests to verify each state independently

### Phase 2: Integration Tests
Run Test 7.x series to verify complete state machine behavior

### Phase 3: Stress Tests
Run extended stress tests with thousands of frames

### Phase 4: Real Hardware Tests
Test with actual USB-TTL adapter and embedded device

## Success Criteria

- All unit tests pass (100% pass rate)
- State coverage: 100% (all 6 states exercised ≥2 times)
- Transition coverage: 100% (all valid transitions exercised)
- No memory leaks detected
- No crashes or undefined behavior
- Parser correctly recovers from all error conditions

## Test Execution Command

```bash
./test.sh
```

Expected output:
```
[==========] Running XX tests from Y test suites
[----------] State Machine Tests
[ RUN      ] StateMachine.WAIT_SOF0_NormalReception
[       OK ] StateMachine.WAIT_SOF0_NormalReception (0 ms)
...
[==========] XX tests from Y test suites ran. (ZZ ms total)
[  PASSED  ] XX tests.
```

## Note on Test Implementation Status

This document specifies 24 tests (19 unit tests + 3 integration tests + 2 additional tests), and **24 tests are implemented** in the test suite.

- **Tests 1.1 through 6.3**: All 19 unit tests are fully implemented and cover the core state machine functionality (WAIT_SOF0, WAIT_SOF1, READ_HEADER, READ_PAYLOAD, VERIFY_CRC, RESYNC states).

- **Integration Tests**:
  - Test 7.1 (Streaming Data with Packet Loss): Implemented as `Integration_MixedFramesWithErrors` - tests mixed valid/invalid frames with error recovery
  - Test 7.2 (Communication with Mixed Frame Types): Implemented as `Integration_MixedFrameTypesRealistic` - tests realistic PING, SET_CFG, STREAM_START/STOP, DATA/RSP/EVT frame sequences with garbage injection
  - Test 7.3 (Stress Test - Rapid State Changes): Implemented as `Integration_StressRapidStateChanges` - tests 1000 iterations with alternating valid/invalid frames, garbage bytes, incomplete frames, CRC errors, and length errors

- **Additional Tests**:
  - `Integration_ByteByByteFeed`: Tests byte-by-byte frame parsing for state transition correctness
  - `Integration_LengthValidation`: Tests invalid length handling (LEN=0 and LEN>4097)

The core state machine logic is thoroughly covered by the existing 24 tests, which exercise all 6 states and their transitions under both normal and stress conditions.

## State Coverage Summary

| State | Target Coverage | Test Cases |
|-------|----------------|------------|
| WAIT_SOF0 | ≥2 times | Test 1.1, 1.1a, 1.2, 1.3 |
| WAIT_SOF1 | ≥2 times | Test 2.1, 2.2, 2.3 |
| READ_HEADER | ≥2 times | Test 3.1, 3.2, 3.3 |
| READ_PAYLOAD | ≥2 times | Test 4.1, 4.2, 4.3 |
| VERIFY_CRC | ≥2 times | Test 5.1, 5.2, 5.3 |
| RESYNC | ≥2 times | Test 6.1, 6.2, 6.3 |

**Total Test Cases**: **24 tests** (19 unit tests in Categories 1-6 + 3 integration tests + 2 additional tests)
