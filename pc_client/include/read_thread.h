#pragma once

#include <atomic>
#include <cstdint>
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
    // USB packet receive timing: EMA and min/max of inter-packet interval (microseconds)
    std::atomic<uint64_t> last_packet_time_us{0};
    std::atomic<uint64_t> ema_interval_us{0};
    std::atomic<uint64_t> min_interval_us{UINT64_MAX};  // not set until first interval
    std::atomic<uint64_t> max_interval_us{0};          // not set until first interval
};

class ReadThread {
public:
    ReadThread(serial::Serial* serial, SampleQueue* sample_q, 
               ResponseQueue* response_q, std::atomic<bool>* stop_flag,
               ThreadStats* stats, int core_id = -1, int rt_prio = -1);
    ~ReadThread();

    ReadThread(const ReadThread&) = delete;
    ReadThread& operator=(const ReadThread&) = delete;

    void start();  // Must be called before sending any commands
    void join();
    bool is_running() const { return running_.load(); }

private:
    void run();
    
    serial::Serial* serial_;
    SampleQueue* sample_q_;
    ResponseQueue* response_q_;
    std::atomic<bool>* stop_flag_;
    ThreadStats* stats_;
    int core_id_;
    int rt_prio_;
    
    std::thread thread_;
    std::atomic<bool> running_{false};
    
    static uint64_t now_steady_us();
};

}  // namespace client
}  // namespace powermonitor
