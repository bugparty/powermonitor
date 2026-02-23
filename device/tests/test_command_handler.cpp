#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>

// Mocks
#include "mocks/pico/stdlib.h"
#include "mocks/mock_ina228.hpp"
#define INA228_HPP

// Define core::RawSample needed by command_handler
#include "core/raw_sample.hpp"

// Device code includes
#include "protocol/frame_defs.hpp"
// Need to include state_machine before command_handler
#include "state_machine.hpp"
#include "command_handler.hpp"

// Static member definition for CommandHandler::tx_buf_
uint8_t device::CommandHandler::tx_buf_[device::CommandHandler::kMaxFrameBytes];

// Helpers for testing
std::vector<uint8_t> tx_buffer;
void mock_write_fn(const uint8_t* data, size_t len) {
    tx_buffer.insert(tx_buffer.end(), data, data + len);
}

void test_time_sync() {
    std::cout << "Testing Time Sync..." << std::endl;

    device::DeviceContext ctx;
    device::CommandHandler handler(ctx, mock_write_fn);

    // Create Time Sync request
    protocol::TimeSyncPayload payload;
    payload.t1 = 1234567890;

    protocol::Frame frame;
    frame.ver = protocol::kProtoVersion;
    frame.type = protocol::FrameType::kCmd;
    frame.msgid = static_cast<uint8_t>(protocol::MsgId::kTimeSync);
    frame.seq = 1;
    frame.len = sizeof(payload) + 1; // +1 for msgid
    frame.data_len = sizeof(payload);
    memcpy(frame.data, &payload, sizeof(payload));

    // Set time
    mock_time_us = 1000000; // T2

    // Execute
    handler.handle_frame(frame);

    // Verify response
    assert(!tx_buffer.empty());

    uint8_t* ptr = tx_buffer.data();
    assert(ptr[0] == protocol::kSof0);
    assert(ptr[1] == protocol::kSof1);

    // uint8_t type = ptr[3];
    // Frame layout: SOF(2) + VER(1) + TYPE(1) + FLAGS(1) + SEQ(1) + LEN(2) + MSGID(1) + Payload...

    // Offset 3 is TYPE.
    assert(ptr[3] == static_cast<uint8_t>(protocol::FrameType::kRsp));

    // Offset 6 is MSGID (which is the original MSGID for response? No, MSGID field of the frame).
    // command_handler sends response with SAME MSGID as request?
    // No, it sends response with MsgId::kTimeSync?
    // command_handler calls build_frame with `msgid`.
    // In handle_time_sync:
    // protocol::build_frame(..., frame.msgid, ...);
    // So the MSGID of the response frame is kTimeSync.
    assert(ptr[8] == static_cast<uint8_t>(protocol::MsgId::kTimeSync));

    // Wait, build_frame puts MSGID at offset 8 (SOF=2, HEADER=6).
    // Header is 6 bytes: VER, TYPE, FLAGS, SEQ, LEN_L, LEN_H.
    // So 2 + 6 = 8.
    // MSGID is the first byte of "Payload" from Parser's perspective, but `build_frame` treats MSGID as separate argument.
    // `build_frame` writes MSGID at `buf[pos++]` where pos starts at 8.
    // So yes, offset 8 is MSGID.

    // Then payload data starts at offset 9.
    // The payload data is `TimeSyncResponsePayload`.
    // struct TimeSyncResponsePayload {
    //    uint8_t orig_msgid;
    //    uint8_t status;
    //    uint64_t t1;
    //    uint64_t t2;
    //    uint64_t t3;
    // }

    const auto* rsp = reinterpret_cast<const protocol::TimeSyncResponsePayload*>(ptr + 9);
    assert(rsp->orig_msgid == static_cast<uint8_t>(protocol::MsgId::kTimeSync));
    assert(rsp->status == static_cast<uint8_t>(protocol::Status::kOk));
    assert(rsp->t1 == 1234567890);
    assert(rsp->t2 == 1000000);
    assert(rsp->t3 >= 1000000); // T3 >= T2

    std::cout << "Time Sync: PASS" << std::endl;
    tx_buffer.clear();
}

void test_time_adjust() {
    std::cout << "Testing Time Adjust..." << std::endl;
    device::DeviceContext ctx;
    ctx.epoch_offset_us = 1000;

    device::CommandHandler handler(ctx, mock_write_fn);

    protocol::TimeAdjustPayload payload;
    payload.offset_us = -500;

    protocol::Frame frame;
    frame.type = protocol::FrameType::kCmd;
    frame.msgid = static_cast<uint8_t>(protocol::MsgId::kTimeAdjust);
    frame.seq = 2;
    frame.len = sizeof(payload) + 1;
    frame.data_len = sizeof(payload);
    memcpy(frame.data, &payload, sizeof(payload));

    handler.handle_frame(frame);

    assert(ctx.epoch_offset_us == 500);
    assert(!tx_buffer.empty());
    // Verify response is OK
    // Response payload: orig_msgid(1) + status(1).
    // MSGID at offset 8 is kTimeAdjust.
    // Payload at offset 9.
    assert(tx_buffer[9] == static_cast<uint8_t>(protocol::MsgId::kTimeAdjust));
    assert(tx_buffer[10] == static_cast<uint8_t>(protocol::Status::kOk));

    std::cout << "Time Adjust: PASS" << std::endl;
    tx_buffer.clear();
}

void test_time_set() {
    std::cout << "Testing Time Set..." << std::endl;
    device::DeviceContext ctx;

    device::CommandHandler handler(ctx, mock_write_fn);

    protocol::TimeSetPayload payload;
    payload.unix_time_us = 2000000;

    protocol::Frame frame;
    frame.type = protocol::FrameType::kCmd;
    frame.msgid = static_cast<uint8_t>(protocol::MsgId::kTimeSet);
    frame.seq = 3;
    frame.len = sizeof(payload) + 1;
    frame.data_len = sizeof(payload);
    memcpy(frame.data, &payload, sizeof(payload));

    mock_time_us = 500000; // Monotonic time

    handler.handle_frame(frame);

    // epoch_offset = unix - monotonic = 2000000 - 500000 = 1500000
    assert(ctx.epoch_offset_us == 1500000);
    assert(!tx_buffer.empty());
    assert(tx_buffer[10] == static_cast<uint8_t>(protocol::Status::kOk));

    std::cout << "Time Set: PASS" << std::endl;
    tx_buffer.clear();
}

void test_stats_report_periodic_and_stop() {
    std::cout << "Testing Stats Report periodic + stop..." << std::endl;
    device::DeviceContext ctx;
    core::SharedContext shared;
    shared.init();
    ctx.shared_ctx = &shared;
    ctx.start_streaming(1000, 0x000F, 0);

    device::CommandHandler handler(ctx, mock_write_fn);
    tx_buffer.clear();

    // First call only initializes cadence baseline.
    mock_time_us = 0;
    handler.maybe_emit_stats_report(mock_time_us);
    assert(tx_buffer.empty());

    // Before 1 second, no report.
    shared.samples_produced = 11;
    shared.samples_dropped = 2;
    shared.dropped_cnvrf_not_ready = 1;
    shared.dropped_duplicate_suppressed = 0;
    shared.dropped_worker_missed_tick = 1;
    shared.dropped_queue_full = 0;
    mock_time_us = 900000;
    handler.maybe_emit_stats_report(mock_time_us);
    assert(tx_buffer.empty());

    // At >=1 second, periodic report should be emitted.
    mock_time_us = 1000000;
    handler.maybe_emit_stats_report(mock_time_us);
    assert(!tx_buffer.empty());
    assert(tx_buffer[3] == static_cast<uint8_t>(protocol::FrameType::kEvt));
    assert(tx_buffer[8] == static_cast<uint8_t>(protocol::MsgId::kStatsReport));
    const auto* periodic = reinterpret_cast<const protocol::StatsReportPayload*>(tx_buffer.data() + 9);
    assert(periodic->report_seq == 0);
    assert(periodic->samples_produced == 11);
    assert(periodic->samples_dropped == 2);
    assert(periodic->dropped_cnvrf_not_ready == 1);
    assert(periodic->dropped_duplicate_suppressed == 0);
    assert(periodic->dropped_worker_missed_tick == 1);
    assert(periodic->dropped_queue_full == 0);
    assert(periodic->window_ms == 1000);
    tx_buffer.clear();

    // STREAM_STOP should send RSP + final STATS_REPORT
    protocol::Frame stop_frame{};
    stop_frame.type = protocol::FrameType::kCmd;
    stop_frame.msgid = static_cast<uint8_t>(protocol::MsgId::kStreamStop);
    stop_frame.seq = 0x2A;
    stop_frame.len = 1;
    stop_frame.data_len = 0;

    shared.samples_produced = 20;
    shared.samples_dropped = 3;
    shared.dropped_cnvrf_not_ready = 1;
    shared.dropped_duplicate_suppressed = 1;
    shared.dropped_worker_missed_tick = 1;
    shared.dropped_queue_full = 0;
    mock_time_us = 1500000;
    handler.handle_frame(stop_frame);

    assert(!tx_buffer.empty());
    // First frame is RSP(STREAM_STOP)
    assert(tx_buffer[3] == static_cast<uint8_t>(protocol::FrameType::kRsp));
    assert(tx_buffer[8] == static_cast<uint8_t>(protocol::MsgId::kStreamStop));

    // Move to second frame (EVT STATS_REPORT)
    const uint16_t rsp_len = static_cast<uint16_t>(tx_buffer[6]) |
                             (static_cast<uint16_t>(tx_buffer[7]) << 8);
    const size_t rsp_total = 2 + 6 + rsp_len + 2;
    assert(tx_buffer.size() > rsp_total + 9);
    const uint8_t* evt = tx_buffer.data() + rsp_total;
    assert(evt[3] == static_cast<uint8_t>(protocol::FrameType::kEvt));
    assert(evt[8] == static_cast<uint8_t>(protocol::MsgId::kStatsReport));
    const auto* final_rsp = reinterpret_cast<const protocol::StatsReportPayload*>(evt + 9);
    assert(final_rsp->report_seq == 1);
    assert(final_rsp->samples_produced == 20);
    assert(final_rsp->samples_dropped == 3);
    assert(final_rsp->dropped_cnvrf_not_ready == 1);
    assert(final_rsp->dropped_duplicate_suppressed == 1);
    assert(final_rsp->dropped_worker_missed_tick == 1);
    assert(final_rsp->dropped_queue_full == 0);
    assert(final_rsp->window_ms == 500);

    std::cout << "Stats Report periodic + stop: PASS" << std::endl;
    tx_buffer.clear();
}

void test_text_report_length_limits() {
    std::cout << "Testing Text Report length limits..." << std::endl;

    device::DeviceContext ctx;
    device::CommandHandler handler(ctx, mock_write_fn);

    // Reject empty payload
    tx_buffer.clear();
    assert(!handler.send_text_report(reinterpret_cast<const uint8_t*>(""), 0));
    assert(tx_buffer.empty());

    // Accept 1-byte payload
    tx_buffer.clear();
    const uint8_t one_byte[] = {'A'};
    assert(handler.send_text_report(one_byte, sizeof(one_byte)));
    assert(!tx_buffer.empty());
    assert(tx_buffer[3] == static_cast<uint8_t>(protocol::FrameType::kEvt));
    assert(tx_buffer[8] == static_cast<uint8_t>(protocol::MsgId::kTextReport));
    uint16_t len = static_cast<uint16_t>(tx_buffer[6]) |
                   (static_cast<uint16_t>(tx_buffer[7]) << 8);
    assert(len == 2); // MSGID(1) + text(1)

    // Accept max 4096-byte payload
    tx_buffer.clear();
    std::vector<uint8_t> max_text(4096, 'x');
    assert(handler.send_text_report(max_text.data(), max_text.size()));
    assert(!tx_buffer.empty());
    assert(tx_buffer[3] == static_cast<uint8_t>(protocol::FrameType::kEvt));
    assert(tx_buffer[8] == static_cast<uint8_t>(protocol::MsgId::kTextReport));
    len = static_cast<uint16_t>(tx_buffer[6]) |
          (static_cast<uint16_t>(tx_buffer[7]) << 8);
    assert(len == 4097); // MSGID(1) + text(4096)

    // Reject oversized payload
    tx_buffer.clear();
    std::vector<uint8_t> too_large(4097, 'y');
    assert(!handler.send_text_report(too_large.data(), too_large.size()));
    assert(tx_buffer.empty());

    std::cout << "Text Report length limits: PASS" << std::endl;
}

int main() {
    test_time_sync();
    test_time_adjust();
    test_time_set();
    test_stats_report_periodic_and_stop();
    test_text_report_length_limits();
    return 0;
}
