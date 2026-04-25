#include <gtest/gtest.h>
#include "node/device_node.h"
#include "node/pc_node.h"
#include "protocol/frame_builder.h"
#include "sim/event_loop.h"
#include "sim/virtual_link.h"

// ===========================
// FUNCTIONAL TESTS
// ===========================
// These tests verify end-to-end functionality of the power monitor system,
// including command handling, data streaming, and error tolerance.

// ---------------------------------------------------------------------------
// Fixture: full PC + Device pair over a clean link
// ---------------------------------------------------------------------------
class PowerMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        sim::LinkConfig config;
        config.min_chunk = 1;
        config.max_chunk = 16;
        config.min_delay_us = 0;
        config.max_delay_us = 2000;
        config.drop_prob = 0.0;
        config.flip_prob = 0.0;
        link.set_pc_to_dev_config(config);
        link.set_dev_to_pc_config(config);

        pc = std::make_unique<node::PCNode>(&link.pc());
        device = std::make_unique<node::DeviceNode>(&link.device());
    }

    void TearDown() override {
        pc.reset();
        device.reset();
    }

    void RunSimulation(uint64_t duration_us, uint64_t tick_interval_us) {
        loop.run_for(duration_us, tick_interval_us, [&](uint64_t now_us) {
            link.pump(now_us);
            pc->tick(now_us);
            device->tick(now_us);
        });
    }

    sim::VirtualLink link;
    sim::EventLoop loop;
    std::unique_ptr<node::PCNode> pc;
    std::unique_ptr<node::DeviceNode> device;
};

// ---------------------------------------------------------------------------
// Fixture: PC only (no device), 100% drop on PC→device direction.
// Used to exercise the timeout/retransmit path without a device to respond.
// ---------------------------------------------------------------------------
class CommandTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        sim::LinkConfig drop_all;
        drop_all.drop_prob = 1.0;
        link.set_pc_to_dev_config(drop_all);
        pc = std::make_unique<node::PCNode>(&link.pc());
    }

    void RunSimulation(uint64_t duration_us, uint64_t tick_interval_us) {
        loop.run_for(duration_us, tick_interval_us, [&](uint64_t now_us) {
            link.pump(now_us);
            pc->tick(now_us);
            // No device — nothing ever acknowledges commands.
        });
    }

    sim::VirtualLink link;
    sim::EventLoop loop;
    std::unique_ptr<node::PCNode> pc;
};

// ===========================================================================
// Happy-path tests
// ===========================================================================

TEST_F(PowerMonitorTest, PingCommand) {
    pc->send_ping(loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
    EXPECT_EQ(pc->orphan_rsp_count(), 0);
}

TEST_F(PowerMonitorTest, SetConfiguration) {
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

TEST_F(PowerMonitorTest, StreamStartStop) {
    pc->send_stream_start(1000, 0x000F, loop.now_us());
    RunSimulation(1'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

TEST_F(PowerMonitorTest, CompleteDataStreamingScenario) {
    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    RunSimulation(10'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0)       << "CRC failures on clean link";
    EXPECT_EQ(pc->data_drop_count(), 0)       << "Data drops on clean link";
    EXPECT_EQ(pc->timeout_count(), 0)         << "Command timeouts on clean link";
    EXPECT_EQ(pc->retransmit_count(), 0)      << "Retransmissions on clean link";
    EXPECT_EQ(pc->orphan_rsp_count(), 0)      << "Orphan RSPs on clean link";
    EXPECT_EQ(pc->error_rsp_count(), 0)       << "Error RSPs on clean link";
    EXPECT_EQ(pc->truncated_data_count(), 0)  << "Truncated DATA frames on clean link";
}

TEST_F(PowerMonitorTest, TimeSynchronizationSequence) {
    constexpr uint8_t kMsgTimeSync   = 0x05;
    constexpr uint8_t kMsgTimeAdjust = 0x06;
    constexpr uint8_t kMsgTimeSet    = 0x07;

    pc->send_time_sync(loop.now_us());
    RunSimulation(200'000, 500);

    EXPECT_EQ(pc->timeout_count(), 0)  << "Timeouts during TIME_SYNC";
    EXPECT_EQ(pc->crc_fail_count(), 0) << "CRC failures during TIME_SYNC";
    EXPECT_EQ(pc->get_rx_count(kMsgTimeSync),   1) << "TIME_SYNC RSP missing";
    EXPECT_EQ(pc->get_rx_count(kMsgTimeAdjust), 1) << "TIME_ADJUST RSP missing";

    pc->send_time_set(1678900000000000ULL, loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->timeout_count(), 0)              << "Timeouts during TIME_SET";
    EXPECT_EQ(pc->get_rx_count(kMsgTimeSet), 1)    << "TIME_SET RSP missing";
}

// ===========================================================================
// CFG_REPORT content verification
// Verifies that handle_cfg_report() correctly parses values sent by the device.
// ===========================================================================

TEST_F(PowerMonitorTest, CfgReportValuesParsedAfterGetCfg) {
    // Start streaming so stream_period_us and stream_mask are set on device.
    pc->send_stream_start(1000, 0x000F, loop.now_us());
    RunSimulation(200'000, 500);

    pc->send_get_cfg(loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->timeout_count(), 0)              << "Timeout waiting for GET_CFG RSP";
    EXPECT_EQ(pc->stream_period_us_cfg(), 1000)    << "stream_period_us not parsed from CFG_REPORT";
    EXPECT_EQ(pc->stream_mask_cfg(),  0x000F)      << "stream_mask not parsed from CFG_REPORT";
    EXPECT_EQ(pc->current_lsb_nA(),  1000U)        << "current_lsb_nA not parsed from CFG_REPORT";

    pc->send_stream_stop(loop.now_us());
    RunSimulation(100'000, 500);
}

// ===========================================================================
// Data sequence number tests
// ===========================================================================

// Verify that the uint8 wraparound of DATA frame seq (255 → 0) is not
// misidentified as a sequence gap. Run streaming long enough to produce >256
// DATA samples so the counter wraps at least once.
TEST_F(PowerMonitorTest, DataSeqWraparoundNoFalseDrops) {
    // Do NOT send SET_CFG during streaming — that triggers an extra CFG_REPORT
    // which increments device data_seq_, creating a perceived gap in DATA seqs.
    pc->send_stream_start(1000, 0x000F, loop.now_us());
    // Allow stream_start to complete, then run 300ms = ~300 DATA samples at 1kHz.
    // Device data_seq_ starts at 2 (after initial CFG_REPORT and TEXT_REPORT),
    // so DATA seqs are 2,3,...,255,0,1,...  The wraparound must not raise a drop.
    RunSimulation(350'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->data_drop_count(), 0) << "Seq uint8 wraparound incorrectly detected as a drop";
    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

// ===========================================================================
// Fault-injection tests — verify errors are detected, not silently ignored
// ===========================================================================

// With 5% packet drops, at least some CRC errors or data drops must be
// detected by the PC. Asserting >= 0 is meaningless; we want > 0.
TEST_F(PowerMonitorTest, PacketDropsCauseDetectableErrors) {
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 16;
    config.min_delay_us = 0;
    config.max_delay_us = 2000;
    config.drop_prob = 0.05;
    config.flip_prob = 0.0;
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    RunSimulation(2'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    // At 5% chunk-level drop with ~3 chunks per DATA frame, ~14% of frames
    // will have at least one dropped chunk, producing CRC errors.
    EXPECT_GT(pc->crc_fail_count(), 0) << "5% drop should produce detectable CRC errors";
}

// With 1% per-bit corruption, a 48-byte DATA frame (384 bits) has ~98%
// probability of containing at least one flipped bit, so CRC errors are
// virtually certain over a 2-second run.
TEST_F(PowerMonitorTest, BitFlipsCauseDetectableCrcErrors) {
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 16;
    config.min_delay_us = 0;
    config.max_delay_us = 2000;
    config.drop_prob = 0.0;
    config.flip_prob = 0.01;
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    RunSimulation(2'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    EXPECT_GT(pc->crc_fail_count(), 0) << "1% bit-flip should produce detectable CRC errors";
}

// ===========================================================================
// Timeout / retransmit tests
// ===========================================================================

// When all PC→device traffic is dropped, the PC must retransmit exactly
// kMaxRetries (3) times and then record a timeout.
TEST_F(CommandTimeoutTest, CommandRetransmitsAndTimesOut) {
    // kCmdTimeoutUs = 200ms, kMaxRetries = 3
    // Timeline: send at t=0, retransmit at t=200ms/400ms/600ms, timeout at t=800ms.
    pc->send_ping(loop.now_us());
    RunSimulation(900'000, 500);

    EXPECT_EQ(pc->timeout_count(),     1) << "Expected exactly 1 timeout after max retries";
    EXPECT_EQ(pc->retransmit_count(),  3) << "Expected exactly 3 retransmits before timeout";
}

// Multiple commands queued simultaneously — each times out independently.
TEST_F(CommandTimeoutTest, MultipleCommandsAllTimeout) {
    pc->send_ping(loop.now_us());
    pc->send_get_cfg(loop.now_us());

    RunSimulation(900'000, 500);

    EXPECT_EQ(pc->timeout_count(),    2) << "Each queued command must time out independently";
    EXPECT_EQ(pc->retransmit_count(), 6) << "3 retransmits per command × 2 commands";
}

// ===========================================================================
// 64-bit timestamp tests
// ===========================================================================

// Verify that timestamps exceeding the old 32-bit limit (~71.6 min) are
// correctly handled without wraparound.
TEST_F(PowerMonitorTest, LongRunningStreamTimestampNoWraparound) {
    // Start streaming at a large base time so that relative timestamps exceed
    // the old uint32_t max (4,294,967,295 µs ≈ 71.6 min).
    // We set stream_start very early and run well past the 32-bit boundary.
    constexpr uint64_t kBaseTime = 100'000;  // 100 ms
    constexpr uint64_t kStreamPeriod = 10'000;  // 10 ms per sample (100 Hz)

    // Fast-forward time to the base, let initial handshake happen
    RunSimulation(kBaseTime, 500);

    pc->send_stream_start(kStreamPeriod, 0x000F, loop.now_us());
    RunSimulation(200'000, 500);  // let stream start settle

    // Now jump ahead past the 32-bit boundary: ~4.3 billion µs = ~71.6 min
    // We simulate in a large step to avoid running billions of ticks.
    // Run 4,300,000,000 µs (~71.7 min) with coarse tick interval.
    constexpr uint64_t kLongDuration = 4'300'000'000ULL;
    RunSimulation(kLongDuration, kStreamPeriod);

    // The last timestamp should be > uint32_t max, proving no wraparound
    EXPECT_GT(pc->last_timestamp_us(), 4'294'967'295ULL)
        << "Timestamp should exceed uint32_t max after 71+ min of streaming";
    EXPECT_EQ(pc->truncated_data_count(), 0)
        << "No DATA frames should be truncated with 64-bit timestamps";
    EXPECT_EQ(pc->crc_fail_count(), 0);
}

// Verify that the sim produces 41-byte DATA_SAMPLE payloads (post 64-bit migration).
TEST_F(PowerMonitorTest, DataSamplePayloadSize41Bytes) {
    pc->send_stream_start(1000, 0x000F, loop.now_us());
    RunSimulation(100'000, 500);

    // If any DATA frames arrived with <41 bytes, truncated_data_count would be > 0
    EXPECT_EQ(pc->truncated_data_count(), 0)
        << "All DATA_SAMPLE frames must have 41-byte payloads";
    // Verify we actually received some data
    EXPECT_GT(pc->get_rx_count(0x80), 0U)
        << "Should have received at least one DATA_SAMPLE";

    pc->send_stream_stop(loop.now_us());
    RunSimulation(100'000, 500);
}

// Verify timestamps are monotonically increasing during continuous streaming.
TEST_F(PowerMonitorTest, DataTimestampsMonotonicallyIncreasing) {
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    // Run 5 seconds of streaming
    RunSimulation(5'000'000, 500);

    // After 5 seconds of 1kHz streaming, timestamp should be roughly 5 million µs
    // (accounting for startup delay and stream_start offset)
    EXPECT_GT(pc->last_timestamp_us(), 4'000'000ULL)
        << "Timestamp should reflect ~5 seconds of streaming";
    EXPECT_EQ(pc->data_drop_count(), 0)
        << "No sequence drops on clean link";

    pc->send_stream_stop(loop.now_us());
    RunSimulation(100'000, 500);
}

// ===========================================================================
// Orphan RSP and Error RSP fault-injection tests
// ===========================================================================

// Fixture for fault-injection tests: PC only (no device), clean link.
// Allows precise control over what frames the PC receives.
class FaultInjectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        sim::LinkConfig clean_config;
        clean_config.drop_prob = 0.0;
        link.set_pc_to_dev_config(clean_config);
        link.set_dev_to_pc_config(clean_config);
        pc = std::make_unique<node::PCNode>(&link.pc());
    }

    void RunSimulation(uint64_t duration_us, uint64_t tick_interval_us) {
        loop.run_for(duration_us, tick_interval_us, [&](uint64_t now_us) {
            link.pump(now_us);
            pc->tick(now_us);
            // No device — we inject frames manually.
        });
    }

    // Inject a raw frame from device to PC.
    void InjectFrame(const std::vector<uint8_t> &frame, uint64_t now_us) {
        link.device().write(frame, now_us);
    }

    sim::VirtualLink link;
    sim::EventLoop loop;
    std::unique_ptr<node::PCNode> pc;
};

// Test that a RSP with no matching pending command increments orphan_rsp_count.
// This simulates a "late" or spurious RSP arriving at the PC.
TEST_F(FaultInjectionTest, OrphanRspDetected) {
    // Build a RSP frame with seq=0x42 (not in pending_ map)
    // RSP payload: orig_msgid (1B) + status (1B)
    std::vector<uint8_t> rsp_payload;
    rsp_payload.push_back(0x01);  // orig_msgid = PING
    rsp_payload.push_back(0x00);  // status = OK
    auto rsp_frame = protocol::build_frame(
        protocol::FrameType::kRsp, 0, 0x42, 0x01, rsp_payload);

    // Inject the RSP frame directly from device to PC via VirtualLink
    InjectFrame(rsp_frame, loop.now_us());

    // Run briefly to let the frame be delivered and parsed
    RunSimulation(10'000, 500);

    EXPECT_EQ(pc->orphan_rsp_count(), 1)
        << "RSP with unmatched seq should increment orphan_rsp_count";
    EXPECT_EQ(pc->error_rsp_count(), 0)
        << "OK status should not increment error_rsp_count";
}

// Test that a RSP with non-OK status increments error_rsp_count.
// We send a command to create a pending entry, then inject an error RSP.
TEST_F(FaultInjectionTest, ErrorRspDetected) {
    // Send PING command - this creates a pending entry with seq=0, msgid=PING
    pc->send_ping(loop.now_us());
    const uint8_t expected_seq = 0;  // First command uses seq=0

    // Build an error RSP with matching seq but non-OK status
    std::vector<uint8_t> rsp_payload;
    rsp_payload.push_back(0x01);  // orig_msgid = PING
    rsp_payload.push_back(0x05);  // status = ERR_HW (hardware fault)
    auto rsp_frame = protocol::build_frame(
        protocol::FrameType::kRsp, 0, expected_seq, 0x01, rsp_payload);

    // Inject the error RSP directly from device endpoint
    InjectFrame(rsp_frame, loop.now_us());

    // Run briefly to let the frame be delivered and parsed
    RunSimulation(10'000, 500);

    EXPECT_EQ(pc->orphan_rsp_count(), 0)
        << "RSP with matching seq/msgid should not increment orphan_rsp_count";
    EXPECT_EQ(pc->error_rsp_count(), 1)
        << "RSP with non-OK status should increment error_rsp_count";
    EXPECT_EQ(pc->timeout_count(), 0)
        << "Error RSP should complete the command without timeout";
}

// Test orphan_rsp_count when orig_msgid mismatches the pending command.
// We send PING, then inject a RSP with correct seq but wrong orig_msgid.
TEST_F(FaultInjectionTest, OrphanRspDetectedOnMsgidMismatch) {
    // Send PING command - this creates a pending entry with seq=0, msgid=PING
    pc->send_ping(loop.now_us());
    const uint8_t expected_seq = 0;  // First command uses seq=0

    // Build a RSP with matching seq but different orig_msgid
    std::vector<uint8_t> rsp_payload;
    rsp_payload.push_back(0x10);  // orig_msgid = SET_CFG (not PING)
    rsp_payload.push_back(0x00);  // status = OK
    auto rsp_frame = protocol::build_frame(
        protocol::FrameType::kRsp, 0, expected_seq, 0x10, rsp_payload);

    // Inject the mismatched RSP
    InjectFrame(rsp_frame, loop.now_us());

    RunSimulation(10'000, 500);

    EXPECT_EQ(pc->orphan_rsp_count(), 1)
        << "RSP with orig_msgid mismatch should increment orphan_rsp_count";
    EXPECT_EQ(pc->error_rsp_count(), 0)
        << "Orphan RSP should not increment error_rsp_count";
}

// Test multiple orphan RSPs are counted correctly.
TEST_F(FaultInjectionTest, MultipleOrphanRspsCounted) {
    // Inject several orphan RSPs with different seq values
    for (uint8_t seq = 0; seq < 5; ++seq) {
        std::vector<uint8_t> rsp_payload;
        rsp_payload.push_back(0x01);  // orig_msgid = PING
        rsp_payload.push_back(0x00);  // status = OK
        auto rsp_frame = protocol::build_frame(
            protocol::FrameType::kRsp, 0, seq, 0x01, rsp_payload);
        InjectFrame(rsp_frame, loop.now_us());
    }

    RunSimulation(10'000, 500);

    EXPECT_EQ(pc->orphan_rsp_count(), 5)
        << "Each orphan RSP should be counted";
}
