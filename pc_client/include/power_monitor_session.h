#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "protocol/frame_builder.h"

namespace serial {
class Serial;
}

namespace powermonitor {
namespace client {

class SampleQueue;
class ResponseQueue;
class ReadThread;
class Session;
struct ThreadStats;

class PowerMonitorSession {
public:
    struct Options {
        std::string port;
        int baud_rate = 115200;
        std::string output_file;
        uint16_t stream_period_us = 1000;
        uint16_t stream_mask = 0x000F;
        bool verbose = false;
        bool interactive = false;
    };
    
    explicit PowerMonitorSession(const Options& options);
    ~PowerMonitorSession();
    
    PowerMonitorSession(const PowerMonitorSession&) = delete;
    PowerMonitorSession& operator=(const PowerMonitorSession&) = delete;
    
    int run();
    void stop();
    
private:
    bool connect_device();
    bool initialize_device();
    bool send_ping();
    bool get_device_config();
    bool configure_device();
    bool start_streaming();
    bool stop_streaming();
    
    bool wait_for_response(uint8_t expected_seq, protocol::Frame* frame, int timeout_ms);
    bool send_command_with_retry(uint8_t msgid, const std::vector<uint8_t>& payload,
                                 std::vector<uint8_t>* rsp_data = nullptr);

    void process_samples_loop();
    void print_statistics(bool inline_mode = false) const;
    void save_and_exit();
 
     Options options_;
     std::unique_ptr<serial::Serial> serial_;
     std::unique_ptr<SampleQueue> sample_queue_;
     std::unique_ptr<ResponseQueue> response_queue_;
     std::unique_ptr<ReadThread> read_thread_;
     std::unique_ptr<Session> session_;
     std::unique_ptr<ThreadStats> stats_;


    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> interrupted_{false};
    uint8_t cmd_seq_ = 0;
    mutable size_t last_stats_width_ = 0;
 };


}  // namespace client
}  // namespace powermonitor
