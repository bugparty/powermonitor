# Testing Guide

## Quick Start

```bash
# One-command test (recommended) — requires PowerShell
pwsh workflow.ps1

# Clean build and test
pwsh workflow.ps1 -Clean

# Verbose output
pwsh workflow.ps1 -Verbose
```

## Web Viewer Only Changes

If your change is limited to `web_viewer/` (React/Vite frontend), you do not need to run `workflow.ps1` or host-side C++ tests.

Use:

```bash
npm install
npm run build:web
```

## Test Script Options

| Option | Description |
|--------|-------------|
| `--clean` | Clean build directory before building |
| `--rebuild` | Same as --clean |
| `-v, --verbose` | Show detailed test output |
| `-h, --help` | Display help message |

## Expected Output

When all tests pass, you should see:

```
[INFO] Power Monitor Test Suite
======================================
[INFO] Configuring CMake...
[SUCCESS] CMake configured
[INFO] Building project...
[SUCCESS] Build completed

[INFO] Running tests...
======================================
[==========] Running 6 tests from 1 test suite.
[  PASSED  ] 6 tests.
======================================
[SUCCESS] All tests passed! ✓
```

## Test Suite Organization

The test suite is organized into two separate files for better maintainability:

### 1. Functional Tests (`functional_test.cpp`)
End-to-end system tests that verify the complete protocol functionality.

**6 Test Cases:**

1. **PingCommand** - Basic connectivity test
   - Validates: Command sending, response receiving, CRC validation

2. **SetConfiguration** - Configuration command handling
   - Validates: Configuration update, CFG_REPORT generation

3. **StreamStartStop** - Data streaming lifecycle
   - Validates: Stream control commands, clean start/stop

4. **CompleteDataStreamingScenario** - End-to-end workflow
   - Validates: Full protocol flow (PING → SET_CFG → STREAM → data collection → STOP)
   - Duration: 10 seconds of streaming at 1kHz
   - Checks: No CRC errors, no timeouts, no retransmissions

5. **CommunicationWithPacketDrops** - Resilience to packet loss
   - Validates: Protocol behavior with 5% packet drop rate
   - Checks: System handles drops gracefully without crashing

6. **CommunicationWithBitFlips** - Resilience to bit errors
   - Validates: Protocol behavior with 1% bit flip rate
   - Checks: CRC detection works, system doesn't crash

### 2. State Machine Tests (`state_machine_test.cpp`)
Low-level parser state machine tests ensuring complete state coverage.

**21 Test Cases** covering all 6 parser states (each state tested ≥2 times):

**Category 1: WAIT_SOF0 State** (3 tests)
- Normal SOF0 reception
- Garbage data rejection
- Recovery from invalid CRC

**Category 2: WAIT_SOF1 State** (3 tests)
- Normal SOF1 reception
- Invalid SOF1 reset
- Self-recovery (AA AA 55 sequence)

**Category 3: READ_HEADER State** (3 tests)
- Complete header reception
- Incomplete header data
- Multiple frame types

**Category 4: READ_PAYLOAD State** (3 tests)
- Complete payload reception
- Incomplete payload data
- Variable length payloads

**Category 5: CRC Verification** (3 tests)
- Valid CRC frame dispatch
- Invalid CRC frame rejection
- Multiple consecutive valid frames

**Category 6: Resync and Error Recovery** (3 tests)
- Find SOF after CRC error
- Garbage data after error
- Partial SOF patterns

**Integration Tests** (3 tests)
- Mixed valid/invalid frames with errors
- Byte-by-byte feed
- Length validation

See [State Machine Test Specification](docs/pc_sim/state_machine_tests.md) for detailed test documentation.

## Manual Testing

If you prefer not to use the test script:

```bash
# Configure
cmake -B build -S .

# Build
cmake --build build

# Run tests
./build/pc_sim/pc_sim_test

# Or use CTest
cd build && ctest --verbose
```

## Troubleshooting

### Tests Fail

1. **Check detailed output**:
   ```bash
   pwsh workflow.ps1 -Verbose
   ```

2. **Look for the failure**:
   - Search for `[  FAILED  ]` in the output
   - Read the error message and assertion failure

3. **Common issues**:
   - **CRC failures**: Check CRC calculation and frame format
   - **Timeouts**: Check event loop timing and timeout values
   - **Data mismatches**: Verify protocol implementation and data encoding

### Build Fails

1. **Clean and rebuild**:
   ```bash
   pwsh workflow.ps1 -Clean
   ```

2. **Check compiler errors**:
   - Missing files? Check file paths in CMakeLists.txt
   - Syntax errors? Fix the code
   - Missing dependencies? Install Google Test (automatically fetched)

### Script Won't Run

1. **Ensure PowerShell is available** (Windows: PowerShell 5.1+, Linux/macOS: `pwsh` from Microsoft):
   ```bash
   pwsh --version
   ```

2. **Run with explicit path** (WSL/Linux):
   ```bash
   pwsh ./workflow.ps1 -Verbose
   ```

## Continuous Integration

For CI/CD pipelines:

```bash
# Exit with error code if tests fail
pwsh workflow.ps1 || exit 1
```

## Test Coverage

Current test coverage (27 tests total):

- ✅ Basic commands (PING, SET_CFG, STREAM_START, STREAM_STOP)
- ✅ Response handling (RSP messages)
- ✅ Event handling (CFG_REPORT)
- ✅ Data streaming (DATA_SAMPLE at 1kHz)
- ✅ Error detection (CRC validation)
- ✅ Fault tolerance (packet drops, bit flips)
- ✅ Protocol state machine (all 4 states, 2+ times each)
- ✅ Parser robustness (fragmentation, corruption)
- ✅ Resync and error recovery

## Adding New Tests

The test suite is split into two files. Choose the appropriate file for your test:

### Adding Functional Tests

For end-to-end system tests, edit `pc_sim/functional_test.cpp`:

```cpp
TEST_F(PowerMonitorTest, YourTestName) {
    // Setup
    pc->send_ping(loop.now_us());

    // Execute
    RunSimulation(100'000, 500);

    // Verify
    EXPECT_EQ(pc->crc_fail_count(), 0);
}
```

### Adding State Machine Tests

For low-level parser tests, edit `pc_sim/state_machine_test.cpp`:

```cpp
TEST_F(ParserStateMachineTest, YourTestName) {
    // Build test frame
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x01, 0x00);
    fb.append_msgid(0x01);

    // Feed to parser
    parser->feed(fb.finalize());

    // Verify
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(frame_count, 1);
}
```

After adding tests, run `pwsh workflow.ps1` to verify they pass.

## Performance Metrics

Typical test execution time:
- Clean build: ~20-30 seconds (includes Google Test download on first run)
- Incremental build: ~2-5 seconds
- Test execution: ~0.1-0.2 seconds

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [Test Details](docs/pc_sim/simulator_tests.md) - Detailed test scenario documentation
- [Protocol Specification](docs/protocol/uart_protocol.md) - Protocol details
