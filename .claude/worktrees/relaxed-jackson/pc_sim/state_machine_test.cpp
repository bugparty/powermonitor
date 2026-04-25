#include <gtest/gtest.h>
#include "protocol/frame_builder.h"
#include "protocol/parser.h"

// ===========================
// STATE MACHINE TESTS
// ===========================
// These tests verify the protocol parser state machine behavior,
// ensuring all states are properly covered and transitions work correctly.
// Reference: docs/pc_sim/state_machine_tests.md

// Helper class for building test frames
class TestFrameBuilder {
public:
    void begin(protocol::FrameType type, uint8_t seq, uint8_t flags) {
        type_ = type;
        seq_ = seq;
        flags_ = flags;
        msgid_ = 0;
        data_.clear();
    }

    void append_msgid(uint8_t msgid) {
        msgid_ = msgid;
    }

    void append_u8(uint8_t val) {
        data_.push_back(val);
    }

    std::vector<uint8_t> finalize() {
        return protocol::build_frame(type_, flags_, seq_, msgid_, data_);
    }

private:
    protocol::FrameType type_ = protocol::FrameType::kCmd;
    uint8_t seq_ = 0;
    uint8_t flags_ = 0;
    uint8_t msgid_ = 0;
    std::vector<uint8_t> data_;
};

// Fixture for parser state machine tests
class ParserStateMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        frame_count = 0;
        parser = std::make_unique<protocol::Parser>([this](const protocol::Frame &f, uint64_t /*receive_time*/) {
            last_frame = f;
            frame_count++;
        }, 4097);
    }

    void TearDown() override {
        parser.reset();
    }

    std::unique_ptr<protocol::Parser> parser;
    protocol::Frame last_frame{};
    int frame_count = 0;
};

// ===== Category 1: WAIT_SOF0 State Coverage =====

TEST_F(ParserStateMachineTest, WAIT_SOF0_InvalidSOF1Handling) {
    // Test 1.1a: Invalid SOF1 handling (regression test)
    // Documents error recovery when SOF1 is invalid
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);

    uint8_t sof0 = 0xAA;
    parser->feed(&sof0, 1);

    // Send invalid SOF1, should reset to WAIT_SOF0
    uint8_t dummy = 0x00;
    parser->feed(&dummy, 1);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

TEST_F(ParserStateMachineTest, WAIT_SOF0_NormalReception) {
    // Test 1.1: Normal SOF0 reception
    // Verify transition from WAIT_SOF0 to WAIT_SOF1 by sending complete SOF sequence
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);

    // Send complete SOF sequence
    uint8_t sof[] = {0xAA, 0x55};
    parser->feed(sof, sizeof(sof));

    // Should transition to READ_HEADER after receiving complete SOF
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadHeader);
}

TEST_F(ParserStateMachineTest, WAIT_SOF0_GarbageRejection) {
    // Test 1.2: Garbage data rejection
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);

    uint8_t garbage[] = {0x00, 0xFF, 0x12, 0x34, 0x56};
    parser->feed(garbage, sizeof(garbage));

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->buffer_size(), 0); // All garbage dropped
}

TEST_F(ParserStateMachineTest, WAIT_SOF0_RecoveryFromInvalidCRC) {
    // Test 1.3: Recovery from invalid frame
    // Build frame with invalid CRC
    std::vector<uint8_t> bad_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x02,                 // TYPE (RSP)
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x01, 0x00,           // LEN = 1
        0x01,                 // MSGID (PING)
        0xFF, 0xFF            // Invalid CRC
    };

    parser->feed(bad_frame);

    // Should be back to WAIT_SOF0 after CRC failure
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->crc_fail_count(), 1);
    EXPECT_EQ(frame_count, 0); // No frame dispatched

    // Now send valid frame - should parse correctly
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x02, 0x00);
    fb.append_msgid(0x01); // PING
    auto valid_frame = fb.finalize();

    parser->feed(valid_frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(frame_count, 1); // Frame dispatched
}

// ===== Category 2: WAIT_SOF1 State Coverage =====

TEST_F(ParserStateMachineTest, WAIT_SOF1_NormalReception) {
    // Test 2.1: Normal SOF1 reception
    std::vector<uint8_t> sof = {0xAA, 0x55};
    parser->feed(sof);

    // After consuming SOF, should be in READ_HEADER state
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadHeader);
}

TEST_F(ParserStateMachineTest, WAIT_SOF1_InvalidReset) {
    // Test 2.2: Invalid SOF1 reset
    std::vector<uint8_t> data = {0xAA, 0x12}; // 0x12 is not valid SOF1
    parser->feed(data);

    // Should reset to WAIT_SOF0
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

TEST_F(ParserStateMachineTest, WAIT_SOF1_SelfRecovery) {
    // Test 2.3: SOF1 self-recovery (AA AA 55 sequence)
    std::vector<uint8_t> data = {0xAA, 0xAA, 0x55};
    parser->feed(data);

    // Should successfully find the AA 55 sequence and enter READ_HEADER
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadHeader);
}

// ===== Category 3: READ_HEADER State Coverage =====

TEST_F(ParserStateMachineTest, READ_HEADER_CompleteReception) {
    // Test 3.1: Complete header reception
    std::vector<uint8_t> data = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x02,                 // TYPE (RSP)
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x03, 0x00            // LEN = 3 (MSGID + 2 data bytes)
    };

    parser->feed(data);

    // Should transition to READ_PAYLOAD
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadPayload);
}

TEST_F(ParserStateMachineTest, READ_HEADER_IncompleteData) {
    // Test 3.2: Incomplete header data
    std::vector<uint8_t> data = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x02,                 // TYPE
        0x00                  // FLAGS only (incomplete)
    };

    parser->feed(data);

    // Should stay in READ_HEADER waiting for more data
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadHeader);
}

TEST_F(ParserStateMachineTest, READ_HEADER_MultipleFrameTypes) {
    // Test 3.3: Multiple valid headers with different frame types

    // CMD frame
    TestFrameBuilder fb1;
    fb1.begin(protocol::FrameType::kCmd, 0x01, 0x00);
    fb1.append_msgid(0x01);
    parser->feed(fb1.finalize());
    EXPECT_EQ(frame_count, 1);

    // RSP frame
    TestFrameBuilder fb2;
    fb2.begin(protocol::FrameType::kRsp, 0x02, 0x00);
    fb2.append_msgid(0x02);
    parser->feed(fb2.finalize());
    EXPECT_EQ(frame_count, 2);

    // DATA frame
    TestFrameBuilder fb3;
    fb3.begin(protocol::FrameType::kData, 0x03, 0x00);
    fb3.append_msgid(0x80);
    parser->feed(fb3.finalize());
    EXPECT_EQ(frame_count, 3);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

// ===== Category 4: READ_PAYLOAD State Coverage =====

TEST_F(ParserStateMachineTest, READ_PAYLOAD_CompleteReception) {
    // Test 4.1: Complete payload reception
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x05, 0x00);
    fb.append_msgid(0x01); // PING
    fb.append_u8(0x00);
    fb.append_u8(0x00);
    auto frame = fb.finalize();

    parser->feed(frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(frame_count, 1);
    EXPECT_EQ(last_frame.msgid, 0x01);
}

TEST_F(ParserStateMachineTest, READ_PAYLOAD_IncompleteData) {
    // Test 4.2: Incomplete payload (send header but not full payload)
    std::vector<uint8_t> data = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x01,                 // TYPE (CMD)
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x0A, 0x00,           // LEN = 10 bytes
        0x01,                 // MSGID
        0x00, 0x00, 0x00      // Only 3 data bytes (need 9 + 2 CRC)
    };

    parser->feed(data);

    // Should be stuck in READ_PAYLOAD waiting for more data
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kReadPayload);
}

TEST_F(ParserStateMachineTest, READ_PAYLOAD_VariableLengths) {
    // Test 4.3: Variable length payloads

    // Short payload (LEN=1, MSGID only)
    TestFrameBuilder fb1;
    fb1.begin(protocol::FrameType::kCmd, 0x01, 0x00);
    fb1.append_msgid(0x01);
    parser->feed(fb1.finalize());
    EXPECT_EQ(frame_count, 1);

    // Medium payload (LEN=10)
    TestFrameBuilder fb2;
    fb2.begin(protocol::FrameType::kCmd, 0x02, 0x00);
    fb2.append_msgid(0x10);
    for (int i = 0; i < 9; i++) {
        fb2.append_u8(i);
    }
    parser->feed(fb2.finalize());
    EXPECT_EQ(frame_count, 2);

    // Large payload (LEN=17, like DATA_SAMPLE)
    TestFrameBuilder fb3;
    fb3.begin(protocol::FrameType::kData, 0x03, 0x00);
    fb3.append_msgid(0x80);
    for (int i = 0; i < 16; i++) {
        fb3.append_u8(i);
    }
    parser->feed(fb3.finalize());
    EXPECT_EQ(frame_count, 3);

    // Max payload (LEN=4097, MSGID + 4096 bytes text)
    TestFrameBuilder fb4;
    fb4.begin(protocol::FrameType::kEvt, 0x04, 0x00);
    fb4.append_msgid(0x93);
    for (int i = 0; i < 4096; ++i) {
        fb4.append_u8(static_cast<uint8_t>(i & 0xFF));
    }
    parser->feed(fb4.finalize());
    EXPECT_EQ(frame_count, 4);

    EXPECT_EQ(parser->crc_fail_count(), 0);
}

// ===== Category 5: CRC Verification Coverage =====

TEST_F(ParserStateMachineTest, CRC_ValidFrame) {
    // Test 5.1: Valid CRC - frame dispatched
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x05, 0x01); // ACK_REQ flag
    fb.append_msgid(0x01); // PING
    auto frame = fb.finalize();

    parser->feed(frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(frame_count, 1);
    EXPECT_EQ(parser->crc_fail_count(), 0);
    EXPECT_EQ(last_frame.type, protocol::FrameType::kCmd);
    EXPECT_EQ(last_frame.msgid, 0x01);
}

TEST_F(ParserStateMachineTest, CRC_InvalidFrame) {
    // Test 5.2: Invalid CRC - not dispatched
    std::vector<uint8_t> bad_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x01,                 // TYPE (CMD)
        0x01,                 // FLAGS
        0x05,                 // SEQ
        0x01, 0x00,           // LEN = 1
        0x01,                 // MSGID (PING)
        0xAB, 0xCD            // Corrupted CRC
    };

    parser->feed(bad_frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->crc_fail_count(), 1);
    EXPECT_EQ(frame_count, 0); // Frame not dispatched
}

TEST_F(ParserStateMachineTest, CRC_MultipleValidFrames) {
    // Test 5.3: Multiple consecutive valid frames
    for (int i = 0; i < 5; i++) {
        TestFrameBuilder fb;
        fb.begin(protocol::FrameType::kCmd, i, 0x00);
        fb.append_msgid(0x01);
        parser->feed(fb.finalize());
    }

    EXPECT_EQ(frame_count, 5);
    EXPECT_EQ(parser->crc_fail_count(), 0);
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

// ===== Category 6: Resync and Error Recovery =====

TEST_F(ParserStateMachineTest, RESYNC_FindSOFAfterError) {
    // Test 6.1: Find valid SOF after CRC error
    std::vector<uint8_t> bad_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x01,                 // TYPE
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x01, 0x00,           // LEN = 1
        0x01,                 // MSGID
        0xFF, 0xFF            // Bad CRC
    };

    // Build valid frame
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x02, 0x00);
    fb.append_msgid(0x01);
    auto good_frame = fb.finalize();

    // Combine bad + good frames
    bad_frame.insert(bad_frame.end(), good_frame.begin(), good_frame.end());

    parser->feed(bad_frame);

    EXPECT_EQ(parser->crc_fail_count(), 1);
    EXPECT_EQ(frame_count, 1); // Only good frame dispatched
}

TEST_F(ParserStateMachineTest, RESYNC_GarbageAfterError) {
    // Test 6.2: Garbage data after CRC error
    std::vector<uint8_t> bad_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x01,                 // TYPE
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x01, 0x00,           // LEN = 1
        0x01,                 // MSGID
        0xFF, 0xFF            // Bad CRC
    };

    // Add garbage data
    uint8_t garbage[] = {0x12, 0x34, 0x56, 0x78};
    bad_frame.insert(bad_frame.end(), garbage, garbage + sizeof(garbage));

    parser->feed(bad_frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->crc_fail_count(), 1);
    EXPECT_EQ(frame_count, 0);
}

TEST_F(ParserStateMachineTest, RESYNC_PartialSOFPattern) {
    // Test 6.3: Partial SOF patterns
    std::vector<uint8_t> data;

    // Garbage with lone 0xAA
    data.push_back(0x12);
    data.push_back(0xAA);
    data.push_back(0x34);

    // Real SOF followed by valid frame
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x01, 0x00);
    fb.append_msgid(0x01);
    auto frame = fb.finalize();

    data.insert(data.end(), frame.begin(), frame.end());

    parser->feed(data);

    EXPECT_EQ(frame_count, 1);
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

// ===== Integration Tests =====

TEST_F(ParserStateMachineTest, Integration_MixedFramesWithErrors) {
    // Test 7.1: Mixed valid/invalid frames with errors
    int total_sent = 0;

    // Send valid frame
    TestFrameBuilder fb1;
    fb1.begin(protocol::FrameType::kCmd, 0x01, 0x00);
    fb1.append_msgid(0x01);
    parser->feed(fb1.finalize());
    total_sent++;

    // Send frame with bad CRC
    std::vector<uint8_t> bad = {
        0xAA, 0x55, 0x01, 0x01, 0x00, 0x02, 0x01, 0x00, 0x01, 0xFF, 0xFF
    };
    parser->feed(bad);
    total_sent++;

    // Send garbage
    uint8_t garbage[] = {0x12, 0x34};
    parser->feed(garbage, sizeof(garbage));

    // Send valid frame
    TestFrameBuilder fb2;
    fb2.begin(protocol::FrameType::kRsp, 0x03, 0x00);
    fb2.append_msgid(0x02);
    parser->feed(fb2.finalize());
    total_sent++;

    EXPECT_EQ(frame_count, 2); // Only 2 valid frames
    EXPECT_EQ(parser->crc_fail_count(), 1);
    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
}

TEST_F(ParserStateMachineTest, Integration_ByteByByteFeed) {
    // Test 7.2: Feed frame byte-by-byte to test state transitions
    TestFrameBuilder fb;
    fb.begin(protocol::FrameType::kCmd, 0x05, 0x00);
    fb.append_msgid(0x01);
    fb.append_u8(0x12);
    fb.append_u8(0x34);
    auto frame = fb.finalize();

    // Feed byte by byte
    for (size_t i = 0; i < frame.size(); i++) {
        parser->feed(&frame[i], 1);
    }

    EXPECT_EQ(frame_count, 1);
    EXPECT_EQ(parser->crc_fail_count(), 0);
}

TEST_F(ParserStateMachineTest, Integration_LengthValidation) {
    // Test 7.3: Invalid length handling
    std::vector<uint8_t> bad_len_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x01,                 // TYPE
        0x00,                 // FLAGS
        0x01,                 // SEQ
        0x00, 0x00            // LEN = 0 (invalid)
    };

    parser->feed(bad_len_frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->len_fail_count(), 1);
    EXPECT_EQ(frame_count, 0);

    // LEN = 4098 (> max 4097) should also be rejected
    std::vector<uint8_t> too_long_frame = {
        0xAA, 0x55,           // SOF
        0x01,                 // VER
        0x04,                 // TYPE (EVT)
        0x00,                 // FLAGS
        0x22,                 // SEQ
        0x02, 0x10            // LEN = 4098 (invalid)
    };

    parser->feed(too_long_frame);

    EXPECT_EQ(parser->get_state(), protocol::Parser::State::kWaitSof0);
    EXPECT_EQ(parser->len_fail_count(), 2);
    EXPECT_EQ(frame_count, 0);
}
