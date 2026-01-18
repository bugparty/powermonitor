#include "power_monitor_session.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <serial/serial.h>
#include <yaml-cpp/yaml.h>

#include "protocol/frame_builder.h"
#include "protocol/unpack.h"
#include "protocol_helpers.h"
#include "read_thread.h"
#include "response_queue.h"
#include "sample_queue.h"
#include "session.h"

namespace powermonitor {
namespace client {

namespace {
std::atomic<bool> g_signal_interrupted{false};

void signal_handler(int) {
    g_signal_interrupted.store(true);
}
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
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "Connecting to device..." << std::endl;
    
    if (!connect_device()) {
        std::cerr << "Failed to connect to device" << std::endl;
        return 1;
    }
    
    std::cout << "Initializing device..." << std::endl;
    
    if (!initialize_device()) {
        std::cerr << "Failed to initialize device" << std::endl;
        return 1;
    }
    
    std::cout << "Streaming started. Press Ctrl+C to stop..." << std::endl;
    
    // 启动采样处理循环
    std::thread processor([this] { process_samples_loop(); });
    
    // 等待中断
    while (!g_signal_interrupted.load() && !stop_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\nStopping..." << std::endl;
    stop_requested_ = true;
    
    if (processor.joinable()) {
        processor.join();
    }
    
    save_and_exit();
    
    std::cout << "Session complete. Samples collected: " 
              << session_->sample_count() << std::endl;
    
    return 0;
}

void PowerMonitorSession::stop() {
    stop_requested_ = true;
    if (read_thread_) {
        read_thread_->join();
    }
}

bool PowerMonitorSession::connect_device() {
    try {
        serial_ = std::make_unique<serial::Serial>(
            options_.port, 
            options_.baud_rate, 
            serial::Timeout::simpleTimeout(100)
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
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
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
    if (!wait_for_response(0x00, &frame, 2000)) {  // CFG_REPORT使用独立的SEQ空间
        std::cerr << "Timeout waiting for CFG_REPORT" << std::endl;
        return false;
    }
    
    if (frame.msgid != 0x91 || frame.data.size() < 16) {
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
    pack_u16(payload.data() + 2, options_.stream_mask);
    
    return send_command_with_retry(0x30, payload);  // STREAM_START
}

bool PowerMonitorSession::send_command_with_retry(uint8_t msgid, 
                                                   const std::vector<uint8_t>& payload,
                                                   std::vector<uint8_t>* rsp_data) {
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
                if (!rsp.data.empty() && rsp.data[0] == 0x00) {  // OK
                    if (rsp_data) {
                        *rsp_data = rsp.data;
                    }
                    return true;
                }
                // 非OK状态，立即失败（不重试）
                if (!rsp.data.empty()) {
                    std::cerr << "Command failed with status: 0x" 
                              << std::hex << static_cast<int>(rsp.data[0]) << std::dec << std::endl;
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
                return true;
            }
            // SEQ不匹配，继续等待（可能是其他响应）
        }
    }
    
    return false;  // 超时
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
        }
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

void PowerMonitorSession::print_statistics() const {
    std::cout << "\n=== Session Statistics ===" << std::endl;
    std::cout << "Samples collected: " << session_->sample_count() << std::endl;
    std::cout << "CRC failures: " << stats_->crc_fail.load() << std::endl;
    std::cout << "Queue overflows: " << stats_->queue_overflow.load() << std::endl;
    std::cout << "Timeouts: " << stats_->timeouts.load() << std::endl;
    
    std::cout << "\nTX counts:" << std::endl;
    for (int i = 0; i < 256; ++i) {
        uint64_t count = stats_->tx_counts[i].load();
        if (count > 0) {
            std::cout << "  MSG 0x" << std::hex << std::setw(2) << std::setfill('0') << i 
                     << std::dec << ": " << count << std::endl;
        }
    }
    
    std::cout << "\nRX counts:" << std::endl;
    for (int i = 0; i < 256; ++i) {
        uint64_t count = stats_->rx_counts[i].load();
        if (count > 0) {
            std::cout << "  MSG 0x" << std::hex << std::setw(2) << std::setfill('0') << i 
                     << std::dec << ": " << count << std::endl;
        }
    }
}

}  // namespace client
}  // namespace powermonitor
