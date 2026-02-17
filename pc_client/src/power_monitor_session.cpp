#include "power_monitor_session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif


#include <CLI/CLI.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <serial/serial.h>

#include "protocol/frame_builder.h"
#include "protocol/unpack.h"
#include "protocol_helpers.h"
#include "read_thread.h"
#include "response_queue.h"
#include "sample_queue.h"
#include "session.h"


namespace powermonitor {
namespace client {

using namespace ftxui;

namespace {
std::atomic<bool> g_signal_interrupted{false};

constexpr uint8_t kMsgTimeSync = 0x05;
constexpr uint8_t kMsgTimeAdjust = 0x06;
constexpr uint8_t kMsgStatsReport = 0x92;
constexpr uint16_t kStreamMaskUsbStressMode = 0x8000;

uint64_t now_steady_us() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void pack_u64_le(std::vector<uint8_t>& dst, uint64_t value) {
    dst.reserve(dst.size() + 8);
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void pack_s64_le(std::vector<uint8_t>& dst, int64_t value) {
    pack_u64_le(dst, static_cast<uint64_t>(value));
}

uint64_t read_u64_le(const std::vector<uint8_t>& buf, size_t offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(buf[offset + static_cast<size_t>(i)]) << (i * 8);
    }
    return value;
}

int64_t signed_diff_u64(uint64_t a, uint64_t b) {
    if (a >= b) {
        return static_cast<int64_t>(a - b);
    }
    return -static_cast<int64_t>(b - a);
}

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        g_signal_interrupted.store(true);
        // 立即返回 TRUE，阻止系统的默认处理和异常
        return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_signal_interrupted.store(true);
        // 给一点时间让主线程响应
        Sleep(1000);
        return TRUE;
    default:
        return FALSE;
    }
}
#else
void signal_handler(int) {
    g_signal_interrupted.store(true);
}
#endif
}  // namespace

PowerMonitorSession::PowerMonitorSession(const Options& options)
    : options_(options) {
    sample_queue_ = std::make_unique<SampleQueue>();
    response_queue_ = std::make_unique<ResponseQueue>();
    session_ = std::make_unique<Session>();
    stats_ = std::make_unique<ThreadStats>();

    // 设置时间戳溢出初始状态
    session_->process_device_timestamp(0);
}

PowerMonitorSession::~PowerMonitorSession() {
    if (read_thread_ && read_thread_->is_running()) {
        stop();
    }
}

int PowerMonitorSession::run() {
    // 注册信号处理
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif

    append_log("Connecting to device...");

    if (!connect_device()) {
        std::cerr << "Failed to connect to device" << std::endl;
        append_log("Failed to connect to device");
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(ui_state_.mutex);
        ui_state_.connected = true;
    }

    append_log("Initializing device...");

    if (!initialize_device()) {
        std::cerr << "Failed to initialize device" << std::endl;
        append_log("Failed to initialize device");
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(ui_state_.mutex);
        ui_state_.initialized = true;
    }

    append_log("Streaming started.");

    // 启动采样处理循环
    std::thread processor([this] { process_samples_loop(); });

    auto next_time_sync = std::chrono::steady_clock::now() + std::chrono::seconds(15);

    int tui_rc = 0;
    if (options_.interactive) {
        tui_rc = run_tui_loop();
    } else {
        auto next_stats_time = std::chrono::steady_clock::now();
        while (!g_signal_interrupted.load() && !stop_requested_.load()) {
            if (streaming_.load() && std::chrono::steady_clock::now() >= next_time_sync) {
                std::string sync_detail;
                if (perform_time_sync_once(&sync_detail)) {
                    append_log("Periodic time sync ok: " + sync_detail);
                } else {
                    append_log("Periodic time sync failed: " + sync_detail);
                }
                next_time_sync = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            }
            if (std::chrono::steady_clock::now() >= next_stats_time) {
                print_statistics(true);
                next_stats_time += std::chrono::milliseconds(100);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::cout << "\r" << std::string(last_stats_width_, ' ') << "\r" << std::flush;
    }

    append_log("Stopping session...");
    stop_requested_ = true;
    sample_queue_->stop();

    if (streaming_.load()) {
        stop_streaming();
    }


    if (processor.joinable()) {
        processor.join();
    }

    save_and_exit();

    std::cout << "Session complete. Samples collected: "
              << session_->sample_count() << std::endl;
    append_log("Session complete.");

    return tui_rc;
}

void PowerMonitorSession::stop() {
    stop_requested_ = true;
    sample_queue_->stop();
    if (read_thread_) {
        read_thread_->join();
    }
}

bool PowerMonitorSession::connect_device() {
    try {
        serial_ = std::make_unique<serial::Serial>(
            options_.port,
            options_.baud_rate,
            serial::Timeout::simpleTimeout(2000),
            serial::bytesize_t::eightbits,
            serial::parity_t::parity_none,
            serial::stopbits_t::stopbits_one,
            serial::flowcontrol_t::flowcontrol_none,
            serial::dtrcontrol_t::dtr_enable,
            serial::rtscontrol_t::rts_enable
        );

        if (!serial_->isOpen()) {
            std::cerr << "Failed to open serial port: " << options_.port << std::endl;
            return false;
        }



        return true;
    } catch (const serial::SerialException& e) {
        std::cerr << "Serial error: " << e.what() << std::endl;
        return false;
    }
}

bool PowerMonitorSession::initialize_device() {
    // 启动读取线程（必须在发送任何命令前启动）
    read_thread_ = std::make_unique<ReadThread>(
        serial_.get(), sample_queue_.get(), response_queue_.get(),
        &stop_requested_, stats_.get()
    );
    read_thread_->start();

    // 留时间让线程启动
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 发送PING
    if (!send_ping()) {
        std::cerr << "PING failed" << std::endl;
        return false;
    }

    std::cout << "Device responded to PING" << std::endl;

    // 获取设备配置
    if (!get_device_config()) {
        std::cerr << "Failed to get device configuration" << std::endl;
        return false;
    }

    std::cout << "Device configuration obtained" << std::endl;

    // Ensure initial calibration runs before any data collection.
    // Device may still be streaming from a previous session, so try to stop it first.
    send_command_with_retry(0x31, {});  // STREAM_STOP (best effort)

    if (!run_time_sync_rounds(3)) {
        std::cerr << "Initial TIME_SYNC failed" << std::endl;
        return false;
    }
    append_log("Initial time sync completed (3 rounds)");

    // Drop any samples that might have arrived before calibration completed.
    SampleQueue::Sample dropped_sample;
    while (sample_queue_->pop(dropped_sample)) {
    }

    // 开始流
    if (!start_streaming()) {
        std::cerr << "Failed to start streaming" << std::endl;
        return false;
    }

    return true;
}

bool PowerMonitorSession::send_ping() {
    return send_command_with_retry(0x01, {});
}

bool PowerMonitorSession::get_device_config() {
    std::vector<uint8_t> rsp_data;
    if (!send_command_with_retry(0x11, {}, &rsp_data)) {  // GET_CFG
        return false;
    }

    // 等待CFG_REPORT (0x91)
    protocol::Frame frame;
    if (!wait_for_message_by_id(0x91, &frame, 2000)) {  // CFG_REPORT使用独立的SEQ空间
        std::cerr << "Timeout waiting for CFG_REPORT" << std::endl;
        return false;
    }

    if (frame.data.size() < 16) {
        std::cerr << "Invalid CFG_REPORT" << std::endl;
        return false;
    }

    Session::Config config;
    config.current_lsb_nA = unpack_u32(frame.data.data() + 2);
    config.adcrange = (frame.data[1] >> 2) & 0x01;
    config.shunt_cal = unpack_u16(frame.data.data() + 6);
    config.config_reg = unpack_u16(frame.data.data() + 8);
    config.adc_config_reg = unpack_u16(frame.data.data() + 10);
    config.stream_period_us = unpack_u16(frame.data.data() + 12);
    config.stream_mask = unpack_u16(frame.data.data() + 14);

    session_->set_config(config);

    return true;
}

bool PowerMonitorSession::start_streaming() {
    std::vector<uint8_t> payload(4);
    pack_u16(payload.data(), options_.stream_period_us);
    uint16_t stream_mask = options_.stream_mask;
    if (options_.usb_stress_mode) {
        stream_mask = static_cast<uint16_t>(stream_mask | kStreamMaskUsbStressMode);
        append_log("USB stress mode enabled (STREAM_START mask bit15)");
    }
    pack_u16(payload.data() + 2, stream_mask);

    const bool ok = send_command_with_retry(0x30, payload);  // STREAM_START
    streaming_.store(ok);
    {
        std::lock_guard<std::mutex> lock(ui_state_.mutex);
        ui_state_.streaming = ok;
    }
    if (ok) {
        append_log("STREAM_START acknowledged");
    }
    return ok;
}

bool PowerMonitorSession::stop_streaming() {
    const bool ok = send_command_with_retry(0x31, {});  // STREAM_STOP
    streaming_.store(false);
    {
        std::lock_guard<std::mutex> lock(ui_state_.mutex);
        ui_state_.streaming = false;
    }
    append_log(ok ? "STREAM_STOP acknowledged" : "STREAM_STOP failed");
    return ok;
}

bool PowerMonitorSession::perform_time_sync_once(std::string* detail) {
    if (detail) {
        detail->clear();
    }

    std::vector<uint8_t> sync_payload;
    pack_u64_le(sync_payload, now_steady_us());

    std::vector<uint8_t> sync_rsp;
    if (!send_command_with_retry(kMsgTimeSync, sync_payload, &sync_rsp)) {
        if (detail) {
            *detail = "TIME_SYNC cmd failed";
        }
        return false;
    }
    if (sync_rsp.size() < 26) {
        if (detail) {
            *detail = "TIME_SYNC rsp too short";
        }
        return false;
    }

    const uint64_t T1 = read_u64_le(sync_rsp, 2);
    const uint64_t T2 = read_u64_le(sync_rsp, 10);
    const uint64_t T3 = read_u64_le(sync_rsp, 18);
    const uint64_t T4 = now_steady_us();

    const int64_t delay = signed_diff_u64(T4, T1) - signed_diff_u64(T3, T2);
    const int64_t offset = (signed_diff_u64(T2, T1) + signed_diff_u64(T3, T4)) / 2;

    std::vector<uint8_t> adjust_payload;
    pack_s64_le(adjust_payload, -offset);
    if (!send_command_with_retry(kMsgTimeAdjust, adjust_payload)) {
        if (detail) {
            std::ostringstream oss;
            oss << "TIME_ADJUST failed offset=" << offset << "us"
                << " delay=" << delay << "us";
            *detail = oss.str();
        }
        return false;
    }

    if (detail) {
        std::ostringstream oss;
        oss << "offset=" << offset << "us"
            << " delay=" << delay << "us"
            << " T1=" << T1 << " T2=" << T2 << " T3=" << T3 << " T4=" << T4;
        *detail = oss.str();
    }
    return true;
}

bool PowerMonitorSession::run_time_sync_rounds(int rounds) {
    if (rounds <= 0) {
        return true;
    }
    for (int i = 0; i < rounds; ++i) {
        if (!perform_time_sync_once()) {
            return false;
        }
    }
    return true;
}

bool PowerMonitorSession::send_command_with_retry(uint8_t msgid,
                                                   const std::vector<uint8_t>& payload,
                                                   std::vector<uint8_t>* rsp_data) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    const int kMaxRetries = 3;
    const int kTimeoutMs = 500;

    for (int retry = 0; retry < kMaxRetries; ++retry) {
        try {
            // 构建帧
            std::vector<uint8_t> frame_data = protocol::build_frame(
                protocol::FrameType::kCmd, 0, cmd_seq_++, msgid, payload
            );
            const uint8_t sent_seq = cmd_seq_ - 1;

            // 发送
            serial_->write(frame_data);
            stats_->tx_counts[msgid].fetch_add(1, std::memory_order_relaxed);

            // 等待响应
            protocol::Frame rsp;
            if (wait_for_response(sent_seq, &rsp, kTimeoutMs)) {
                // RSP DATA = [orig_msgid, status, extra...]
                // data[0] = orig_msgid, data[1] = status
                if (rsp.data.size() >= 2 && rsp.data[1] == 0x00) {  // OK
                    if (rsp_data) {
                        *rsp_data = rsp.data;
                    }
                    return true;
                }
                // 非OK状态，立即失败（不重试）
                if (rsp.data.size() >= 2) {
                    std::cerr << "Command failed with status: 0x"
                              << std::hex << static_cast<int>(rsp.data[1]) << std::dec << std::endl;
                }
                return false;
            }

            // 超时，准备重试
            std::cerr << "Command timeout, retry " << (retry + 1) << "/" << kMaxRetries << std::endl;
            stats_->timeouts.fetch_add(1, std::memory_order_relaxed);

        } catch (const serial::SerialException& e) {
            std::cerr << "Serial error: " << e.what() << std::endl;
            return false;
        }
    }

    return false;
}

bool PowerMonitorSession::wait_for_response(uint8_t expected_seq,
                                             protocol::Frame* frame,
                                             int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()
            ).count()
        );

        remaining_ms = std::max(remaining_ms, 0);

        if (response_queue_->pop_wait(*frame, remaining_ms)) {
            if (frame->seq == expected_seq) {
                if (frame->msgid == kMsgStatsReport) {
                    process_async_control_frame(*frame);
                    continue;
                }
                return true;
            }
            // SEQ不匹配，继续等待（可能是其他响应）
            process_async_control_frame(*frame);
        }
    }

    return false;  // 超时
}

bool PowerMonitorSession::wait_for_message_by_id(uint8_t expected_msgid,
                                                  protocol::Frame* frame,
                                                  int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()
            ).count()
        );

        remaining_ms = std::max(remaining_ms, 0);

        if (response_queue_->pop_wait(*frame, remaining_ms)) {
            if (frame->msgid == expected_msgid) {
                return true;
            }
            // msgid不匹配，继续等待（可能是其他消息）
            process_async_control_frame(*frame);
        }
    }

    return false;  // 超时
}

void PowerMonitorSession::process_async_control_frame(const protocol::Frame& frame) {
    if (frame.msgid != kMsgStatsReport) {
        return;
    }
    if (frame.data.size() < 31) {
        append_log("STATS_REPORT ignored: payload too short");
        return;
    }

    const uint16_t report_seq = unpack_u16(frame.data.data());
    const uint32_t produced = unpack_u32(frame.data.data() + 2);
    const uint32_t dropped = unpack_u32(frame.data.data() + 6);
    const uint32_t dropped_cnvrf_not_ready = unpack_u32(frame.data.data() + 10);
    const uint32_t dropped_duplicate_suppressed = unpack_u32(frame.data.data() + 14);
    const uint32_t dropped_worker_missed_tick = unpack_u32(frame.data.data() + 18);
    const uint32_t dropped_queue_full = unpack_u32(frame.data.data() + 22);
    const uint16_t queue_depth = unpack_u16(frame.data.data() + 26);
    const uint8_t reason_bits = frame.data[28];
    const uint16_t window_ms = unpack_u16(frame.data.data() + 29);

    {
        std::lock_guard<std::mutex> lock(ui_state_.mutex);
        ui_state_.has_stats_report = true;
        ui_state_.stats_report_seq = report_seq;
        ui_state_.stats_samples_produced = produced;
        ui_state_.stats_samples_dropped = dropped;
        ui_state_.stats_dropped_cnvrf_not_ready = dropped_cnvrf_not_ready;
        ui_state_.stats_dropped_duplicate_suppressed = dropped_duplicate_suppressed;
        ui_state_.stats_dropped_worker_missed_tick = dropped_worker_missed_tick;
        ui_state_.stats_dropped_queue_full = dropped_queue_full;
        ui_state_.stats_queue_depth = queue_depth;
        ui_state_.stats_reason_bits = reason_bits;
        ui_state_.stats_window_ms = window_ms;
    }

    std::ostringstream oss;
    oss << "STATS_REPORT seq=" << report_seq
        << " produced=" << produced
        << " dropped=" << dropped
        << " q_depth=" << queue_depth
        << " reason=0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(reason_bits)
        << std::dec << std::setfill(' ')
        << " window_ms=" << window_ms;
    append_log(oss.str());
}

void PowerMonitorSession::process_samples_loop() {
    SampleQueue::Sample sample;

    while (!stop_requested_.load()) {
        if (sample_queue_->pop_wait(sample)) {
            if (sample.raw_data.size() >= 16) {
                Session::Sample session_sample;
                session_sample.seq = sample.seq;
                session_sample.host_timestamp_us = sample.host_timestamp_us;
                session_sample.device_timestamp_us = session_->process_device_timestamp(sample.device_timestamp_us);
                session_sample.flags = sample.raw_data[4];

                session_sample.vbus_raw = protocol::unpack_u20(sample.raw_data.data() + 5);
                session_sample.vshunt_raw = protocol::unpack_s20(sample.raw_data.data() + 8);
                session_sample.current_raw = protocol::unpack_s20(sample.raw_data.data() + 11);
                session_sample.temp_raw = unpack_s16(sample.raw_data.data() + 14);

                session_->add_sample(session_sample);
                sample_counter_.fetch_add(1, std::memory_order_relaxed);
                if ((session_sample.flags & 0x10U) == 0) {
                    std::lock_guard<std::mutex> lock(ui_state_.mutex);
                    ui_state_.latest_sample = session_sample;
                    ui_state_.has_latest_sample = true;
                }
            }
        }
    }

    // 处理队列中剩余的所有采样
    while (sample_queue_->pop(sample)) {
        if (sample.raw_data.size() >= 16) {
            Session::Sample session_sample;
            session_sample.seq = sample.seq;
            session_sample.host_timestamp_us = sample.host_timestamp_us;
            session_sample.device_timestamp_us = session_->process_device_timestamp(sample.device_timestamp_us);
            session_sample.flags = sample.raw_data[4];

            session_sample.vbus_raw = protocol::unpack_u20(sample.raw_data.data() + 5);
            session_sample.vshunt_raw = protocol::unpack_s20(sample.raw_data.data() + 8);
            session_sample.current_raw = protocol::unpack_s20(sample.raw_data.data() + 11);
            session_sample.temp_raw = unpack_s16(sample.raw_data.data() + 14);

            session_->add_sample(session_sample);
            sample_counter_.fetch_add(1, std::memory_order_relaxed);
            if ((session_sample.flags & 0x10U) == 0) {
                std::lock_guard<std::mutex> lock(ui_state_.mutex);
                ui_state_.latest_sample = session_sample;
                ui_state_.has_latest_sample = true;
            }
        }
    }
}

int PowerMonitorSession::run_tui_loop() {
    auto sum_counts = [](const std::atomic<uint64_t> counts[256]) {
        uint64_t total = 0;
        for (int i = 0; i < 256; ++i) {
            total += counts[i].load(std::memory_order_relaxed);
        }
        return total;
    };

    ScreenInteractive screen = ScreenInteractive::TerminalOutput();

    Component renderer = Renderer([&] {
        bool connected = false;
        bool initialized = false;
        bool streaming = false;
        bool has_latest_sample = false;
        bool has_stats_report = false;
        Session::Sample latest_sample{};
        uint16_t stats_report_seq = 0;
        uint32_t stats_samples_produced = 0;
        uint32_t stats_samples_dropped = 0;
        uint32_t stats_dropped_cnvrf_not_ready = 0;
        uint32_t stats_dropped_duplicate_suppressed = 0;
        uint32_t stats_dropped_worker_missed_tick = 0;
        uint32_t stats_dropped_queue_full = 0;
        uint16_t stats_queue_depth = 0;
        uint8_t stats_reason_bits = 0;
        uint16_t stats_window_ms = 0;
        std::deque<std::string> logs;
        {
            std::lock_guard<std::mutex> lock(ui_state_.mutex);
            connected = ui_state_.connected;
            initialized = ui_state_.initialized;
            streaming = ui_state_.streaming;
            has_latest_sample = ui_state_.has_latest_sample;
            has_stats_report = ui_state_.has_stats_report;
            latest_sample = ui_state_.latest_sample;
            stats_report_seq = ui_state_.stats_report_seq;
            stats_samples_produced = ui_state_.stats_samples_produced;
            stats_samples_dropped = ui_state_.stats_samples_dropped;
            stats_dropped_cnvrf_not_ready = ui_state_.stats_dropped_cnvrf_not_ready;
            stats_dropped_duplicate_suppressed = ui_state_.stats_dropped_duplicate_suppressed;
            stats_dropped_worker_missed_tick = ui_state_.stats_dropped_worker_missed_tick;
            stats_dropped_queue_full = ui_state_.stats_dropped_queue_full;
            stats_queue_depth = ui_state_.stats_queue_depth;
            stats_reason_bits = ui_state_.stats_reason_bits;
            stats_window_ms = ui_state_.stats_window_ms;
            logs = ui_state_.logs;
        }

        const uint64_t samples = sample_counter_.load(std::memory_order_relaxed);
        const uint64_t crc_fail = stats_->crc_fail.load(std::memory_order_relaxed);
        const uint64_t queue_overflow = stats_->queue_overflow.load(std::memory_order_relaxed);
        const uint64_t timeouts = stats_->timeouts.load(std::memory_order_relaxed);
        const uint64_t io_errors = stats_->io_errors.load(std::memory_order_relaxed);
        const uint64_t tx_total = sum_counts(stats_->tx_counts);
        const uint64_t rx_total = sum_counts(stats_->rx_counts);

        Elements log_lines;
        if (logs.empty()) {
            log_lines.push_back(text("(no logs yet)"));
        } else {
            for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
                const auto& line = *it;
                log_lines.push_back(text(line));
            }
        }

        std::string latest = "No sample yet";
        if (has_latest_sample) {
            const auto cfg = session_->get_config();
            const double vbus_v = latest_sample.vbus_raw * 195.3125e-6;
            const double current_a = latest_sample.current_raw * (cfg.current_lsb_nA * 1e-9);
            const double temp_c = latest_sample.temp_raw * 7.8125e-3;
            std::ostringstream oss;
            oss << "SEQ=" << std::setw(3) << std::setfill('0')
                << static_cast<unsigned>(latest_sample.seq) << std::setfill(' ')
                << "  VBUS=" << std::fixed
                << std::setprecision(3) << vbus_v << " V"
                << "  I=" << std::setprecision(4) << current_a << " A"
                << "  T=" << std::setprecision(2) << temp_c << " C";
            latest = oss.str();
        }

        const std::string state_line =
            std::string("connected=") + (connected ? "yes" : "no") +
            " initialized=" + (initialized ? "yes" : "no") +
            " streaming=" + (streaming ? "yes" : "no");

        std::string stats_line = "stats_report=(none)";
        if (has_stats_report) {
            std::ostringstream oss;
            oss << "stats_report: seq=" << stats_report_seq
                << " produced=" << stats_samples_produced
                << " dropped=" << stats_samples_dropped
                << " cnvrf=" << stats_dropped_cnvrf_not_ready
                << " dup=" << stats_dropped_duplicate_suppressed
                << " missed=" << stats_dropped_worker_missed_tick
                << " qfull=" << stats_dropped_queue_full
                << " q_depth=" << stats_queue_depth
                << " reason=0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(stats_reason_bits)
                << std::dec << std::setfill(' ')
                << " window_ms=" << stats_window_ms;
            stats_line = oss.str();
        }

        return vbox({
                   text("Host PC Client - FTXUI") | bold,
                   separator(),
                   text(state_line),
                   text(stats_line),
                   text("samples=" + std::to_string(samples) + " rx=" + std::to_string(rx_total) +
                        " tx=" + std::to_string(tx_total) + " crc_fail=" + std::to_string(crc_fail) +
                        " timeout=" + std::to_string(timeouts) + " io_err=" + std::to_string(io_errors) +
                        " q_overflow=" + std::to_string(queue_overflow)),
                   text(latest),
                   separator(),
                   text("Keys: [t]=toggle stream  [s]=save snapshot  [q]=quit") | dim,
                   separator(),
                   text("Logs (newest first):") | bold,
                   vbox(std::move(log_lines)) | yframe | size(HEIGHT, LESS_THAN, 12),
               }) |
               border;
    });

    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            stop_requested_.store(true);
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('s') || event == Event::Character('S')) {
            save_snapshot(options_.output_file);
            return true;
        }
        if (event == Event::Character('t') || event == Event::Character('T')) {
            if (streaming_.load()) {
                stop_streaming();
            } else {
                start_streaming();
            }
            return true;
        }
        return false;
    });

    std::thread refresher([&] {
        constexpr int kTuiTimeSyncPeriodSec = 5;
        auto next_time_sync =
            std::chrono::steady_clock::now() + std::chrono::seconds(kTuiTimeSyncPeriodSec);
        while (!stop_requested_.load()) {
            if (g_signal_interrupted.load()) {
                stop_requested_.store(true);
                screen.PostEvent(Event::Character('q'));
                return;
            }

            // Drain asynchronous STATS_REPORT frames continuously in TUI mode,
            // so they appear near real-time instead of being processed only
            // when command-response waits happen (e.g. TIME_SYNC every 15s).
            protocol::Frame async_frame;
            while (response_queue_->pop_by_msgid(async_frame, kMsgStatsReport)) {
                process_async_control_frame(async_frame);
            }

            if (streaming_.load() && std::chrono::steady_clock::now() >= next_time_sync) {
                std::string sync_detail;
                if (perform_time_sync_once(&sync_detail)) {
                    append_log("Periodic time sync ok: " + sync_detail);
                } else {
                    append_log("Periodic time sync failed: " + sync_detail);
                }
                next_time_sync =
                    std::chrono::steady_clock::now() + std::chrono::seconds(kTuiTimeSyncPeriodSec);
            }

            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        screen.PostEvent(Event::Character('q'));
    });

    screen.Loop(renderer);
    if (refresher.joinable()) {
        refresher.join();
    }
    return 0;
}

bool PowerMonitorSession::save_snapshot(const std::string& path) {
    if (path.empty()) {
        append_log("Save skipped: output path is empty");
        return false;
    }
    try {
        session_->save(path);
        append_log("Saved snapshot to " + path);
        return true;
    } catch (const std::exception& e) {
        append_log(std::string("Save failed: ") + e.what());
        return false;
    }
}

void PowerMonitorSession::append_log(const std::string& message) {
    std::lock_guard<std::mutex> lock(ui_state_.mutex);
    ui_state_.logs.push_back(message);
    constexpr size_t kMaxLogs = 200;
    while (ui_state_.logs.size() > kMaxLogs) {
        ui_state_.logs.pop_front();
    }
}

void PowerMonitorSession::save_and_exit() {
    if (!options_.output_file.empty()) {
        std::cout << "Saving data to " << options_.output_file << std::endl;
        try {
            session_->save(options_.output_file);
        } catch (const std::exception& e) {
            std::cerr << "Failed to save data: " << e.what() << std::endl;
        }
    }

    if (options_.verbose) {
        print_statistics();
    }
}

void PowerMonitorSession::print_statistics(bool inline_mode) const {
    auto sum_counts = [](const std::atomic<uint64_t> counts[256]) {
        uint64_t total = 0;
        for (int i = 0; i < 256; ++i) {
            total += counts[i].load(std::memory_order_relaxed);
        }
        return total;
    };

    const uint64_t samples = session_->sample_count();
    const uint64_t crc_fail = stats_->crc_fail.load(std::memory_order_relaxed);
    const uint64_t queue_overflow = stats_->queue_overflow.load(std::memory_order_relaxed);
    const uint64_t timeouts = stats_->timeouts.load(std::memory_order_relaxed);
    const uint64_t io_errors = stats_->io_errors.load(std::memory_order_relaxed);
    const uint64_t tx_total = sum_counts(stats_->tx_counts);
    const uint64_t rx_total = sum_counts(stats_->rx_counts);

    if (inline_mode) {
        std::ostringstream oss;
        oss << "Samples:" << std::setw(10) << samples
            << "  CRC_FAIL:" << std::setw(6) << crc_fail
            << "  RX:" << std::setw(8) << rx_total
            << "  TX:" << std::setw(8) << tx_total
            << "  Timeouts:" << std::setw(5) << timeouts
            << "  IO_Errors:" << std::setw(4) << io_errors
            << "  QueueOverflow:" << std::setw(4) << queue_overflow;
        std::string line = oss.str();
        if (line.size() < last_stats_width_) {
            line.append(last_stats_width_ - line.size(), ' ');
        } else {
            last_stats_width_ = line.size();
        }
        std::cout << "\r" << line << std::flush;
        return;
    }

    std::cout << "\n=== Session Statistics ===" << std::endl;
    std::cout << "Samples collected: " << samples << std::endl;
    std::cout << "CRC failures: " << crc_fail << std::endl;
    std::cout << "Queue overflows: " << queue_overflow << std::endl;
    std::cout << "Timeouts: " << timeouts << std::endl;
    std::cout << "IO errors: " << io_errors << std::endl;

    std::cout << "\nTX counts:" << std::endl;
    for (int i = 0; i < 256; ++i) {
        uint64_t count = stats_->tx_counts[i].load(std::memory_order_relaxed);
        if (count > 0) {
            std::cout << "  MSG 0x" << std::hex << std::setw(2) << std::setfill('0') << i
                      << std::dec << ": " << count << std::endl;
        }
    }

    std::cout << "\nRX counts:" << std::endl;
    for (int i = 0; i < 256; ++i) {
        uint64_t count = stats_->rx_counts[i].load(std::memory_order_relaxed);
        if (count > 0) {
            std::cout << "  MSG 0x" << std::hex << std::setw(2) << std::setfill('0') << i
                      << std::dec << ": " << count << std::endl;
        }
    }
}


}  // namespace client
}  // namespace powermonitor
