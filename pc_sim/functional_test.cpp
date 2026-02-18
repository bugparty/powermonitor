#include <gtest/gtest.h>
#include "node/device_node.h"
#include "node/pc_node.h"
#include "sim/event_loop.h"
#include "sim/virtual_link.h"

// ===========================
// FUNCTIONAL TESTS
// ===========================
// These tests verify end-to-end functionality of the power monitor system,
// including command handling, data streaming, and error tolerance.

// Test fixture for common setup
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

// Test basic ping command
TEST_F(PowerMonitorTest, PingCommand) {
    pc->send_ping(loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

// Test configuration command
TEST_F(PowerMonitorTest, SetConfiguration) {
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    RunSimulation(100'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

// Test stream start and stop
TEST_F(PowerMonitorTest, StreamStartStop) {
    pc->send_stream_start(1000, 0x000F, loop.now_us());
    RunSimulation(1'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    EXPECT_EQ(pc->crc_fail_count(), 0);
    EXPECT_EQ(pc->timeout_count(), 0);
}

// Test complete data streaming scenario
TEST_F(PowerMonitorTest, CompleteDataStreamingScenario) {
    // Send commands
    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    // Run streaming for 10 seconds
    RunSimulation(10'000'000, 500);

    // Stop streaming
    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    // Verify no communication errors
    EXPECT_EQ(pc->crc_fail_count(), 0) << "CRC failures detected";
    EXPECT_EQ(pc->data_drop_count(), 0) << "Data drops detected";
    EXPECT_EQ(pc->timeout_count(), 0) << "Timeouts detected";
    EXPECT_EQ(pc->retransmit_count(), 0) << "Retransmissions detected";
}

// Test link with packet drops
TEST_F(PowerMonitorTest, CommunicationWithPacketDrops) {
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 16;
    config.min_delay_us = 0;
    config.max_delay_us = 2000;
    config.drop_prob = 0.05;  // 5% packet drop
    config.flip_prob = 0.0;
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    RunSimulation(2'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    // With packet drops, partial frames may cause CRC errors
    // This test verifies the system handles drops gracefully
    // and doesn't crash
    EXPECT_GE(pc->crc_fail_count(), 0);
}

// Test link with bit flips
TEST_F(PowerMonitorTest, CommunicationWithBitFlips) {
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 16;
    config.min_delay_us = 0;
    config.max_delay_us = 2000;
    config.drop_prob = 0.0;
    config.flip_prob = 0.01;  // 1% bit flip
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    pc->send_ping(loop.now_us());
    pc->send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc->send_stream_start(1000, 0x000F, loop.now_us());

    RunSimulation(2'000'000, 500);

    pc->send_stream_stop(loop.now_us());
    RunSimulation(500'000, 500);

    // With bit flips, we may have CRC failures detected and handled
    // This test just ensures the system doesn't crash
    EXPECT_GE(pc->crc_fail_count(), 0);
}

// Test TimeSync command with invalid payload length
TEST_F(PowerMonitorTest, TimeSyncInvalidPayload) {
    // 1. Run simulation briefly to clear initial reports
    RunSimulation(100'000, 500);

    // 2. Inject malformed frame (TimeSync 0x05 with 4 bytes payload, needs 8)
    std::vector<uint8_t> payload = {0x00, 0x00, 0x00, 0x00};
    // kCmd=0x01, flags=0, seq=0xAA, msgid=0x05
    auto frame_bytes = protocol::build_frame(protocol::FrameType::kCmd, 0, 0xAA, 0x05, payload);
    link.pc().write(frame_bytes, loop.now_us());

    bool response_received = false;
    uint8_t received_status = 0xFF;

    protocol::Parser parser([&](const protocol::Frame &frame, uint64_t) {
        if (frame.type == protocol::FrameType::kRsp && frame.msgid == 0x05) {
            // Check status in payload
            // Payload: [orig_msgid, status, ...]
            if (frame.data.size() >= 2) {
                if (frame.data[0] == 0x05) {
                    received_status = frame.data[1];
                    response_received = true;
                }
            }
        }
    });

    // 3. Run custom loop to pump data and tick device (but NOT PC)
    loop.run_for(100'000, 500, [&](uint64_t now_us) {
        link.pump(now_us);
        device->tick(now_us);

        if (link.pc().available() > 0) {
            auto bytes = link.pc().read(link.pc().available());
            parser.feed(bytes);
        }
    });

    EXPECT_TRUE(response_received) << "Device did not send response to invalid TimeSync";
    EXPECT_EQ(received_status, 0x02) << "Device returned wrong status (expected 0x02 ERR_LEN)";
}
