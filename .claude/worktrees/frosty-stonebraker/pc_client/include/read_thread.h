#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "protocol/parser.h"
#include "sample_queue.h"
#include "response_queue.h"

namespace serial {
class Serial;
}

namespace powermonitor {
namespace client {

struct ThreadStats {
    std::atomic<uint64_t> rx_counts[256]{};
    std::atomic<uint64_t> tx_counts[256]{};
    std::atomic<uint64_t> crc_fail{0};
    std::atomic<uint64_t> queue_overflow{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> retries{0};
    std::atomic<uint64_t> io_errors{0};
};

class ReadThread {
public:
    ReadThread(serial::Serial* serial, SampleQueue* sample_q, 
               ResponseQueue* response_q, std::atomic<bool>* stop_flag,
               ThreadStats* stats);
    ~ReadThread();

    ReadThread(const ReadThread&) = delete;
    ReadThread& operator=(const ReadThread&) = delete;

    void start();  // 必须在发送任何命令前启动
    void join();
    bool is_running() const { return running_.load(); }

private:
    void run();
    
    serial::Serial* serial_;
    SampleQueue* sample_q_;
    ResponseQueue* response_q_;
    std::atomic<bool>* stop_flag_;
    ThreadStats* stats_;
    
    std::thread thread_;
    std::atomic<bool> running_{false};
    
    static uint64_t now_steady_us();
};

}  // namespace client
}  // namespace powermonitor
