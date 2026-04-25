#include "read_thread.h"
#include "thread_affinity.h"

#include <chrono>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <serial/serial.h>

#include "protocol/unpack.h"
#include "protocol_helpers.h"
namespace powermonitor {
namespace client {

ReadThread::ReadThread(serial::Serial* serial, SampleQueue* sample_q,
                       ResponseQueue* response_q, std::atomic<bool>* stop_flag,
                       ThreadStats* stats, int core_id, int rt_prio)
    : serial_(serial),
      sample_q_(sample_q),
      response_q_(response_q),
      stop_flag_(stop_flag),
      stats_(stats),
      core_id_(core_id),
      rt_prio_(rt_prio) {
}

ReadThread::~ReadThread() {
    if (running_.load() || thread_.joinable()) {
        join();
    }
}

void ReadThread::start() {
    if (running_.load()) {
        return;
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void ReadThread::join() {
    if (running_.load()) {
        serial_->stop_async_read();
        serial_->stop_io();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}


namespace {
constexpr uint8_t kMsgDataSample = 0x80;
constexpr uint8_t kMsgTextReport = 0x93;

uint64_t now_unix_us_local() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

struct ParserState {
    protocol::Parser parser;
    ResponseQueue* response_q = nullptr;
    SampleQueue* sample_q = nullptr;
    ThreadStats* stats = nullptr;

    ParserState(ResponseQueue* rq, SampleQueue* sq, ThreadStats* st)
        : parser([this](const protocol::Frame& frame, uint64_t receive_time_us) {
            this->on_frame(frame, receive_time_us);
          }),
          response_q(rq), sample_q(sq), stats(st) {}

    void on_frame(const protocol::Frame& frame, uint64_t receive_time_us) {
        stats->rx_counts[frame.msgid].fetch_add(1, std::memory_order_relaxed);

        // EMA and min/max of inter-packet interval (average delay between consecutive USB packets)
        const uint64_t last = stats->last_packet_time_us.exchange(receive_time_us, std::memory_order_relaxed);
        if (last != 0) {
            const uint64_t interval_us = receive_time_us - last;
            const double ema_old = static_cast<double>(stats->ema_interval_us.load(std::memory_order_relaxed));
            const double ema_new = 0.9 * ema_old + 0.1 * static_cast<double>(interval_us);
            stats->ema_interval_us.store(static_cast<uint64_t>(ema_new), std::memory_order_relaxed);
            // Only update min when interval > 0 (avoid min=0 when two packets land in same microsecond)
            uint64_t min_u = stats->min_interval_us.load(std::memory_order_relaxed);
            if (interval_us > 0 && interval_us < min_u) {
                stats->min_interval_us.store(interval_us, std::memory_order_relaxed);
            }
            uint64_t max_u = stats->max_interval_us.load(std::memory_order_relaxed);
            if (interval_us > max_u) {
                stats->max_interval_us.store(interval_us, std::memory_order_relaxed);
            }
        }

        if (frame.msgid == kMsgDataSample) {  // DATA_SAMPLE
            if (frame.data.size() < 16) {
                stats->crc_fail.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            // Only support new 28-byte format: timestamp_unix_us(8) + timestamp_us(8) + flags(1) + vbus20(3) + vshunt20(3) + current20(3) + dietemp16(2)
            if (frame.data.size() < 28) {
                stats->crc_fail.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            SampleQueue::Sample sample;
            sample.seq = frame.seq;
            sample.host_timestamp_us = receive_time_us;
            sample.host_timestamp_unix_us = now_unix_us_local();
            sample.device_timestamp_unix_us = unpack_u64(frame.data.data());
            sample.device_timestamp_us = unpack_u64(frame.data.data() + 8);
            sample.raw_data = frame.data;

            if (!sample_q->push(std::move(sample))) {
                stats->queue_overflow.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (frame.type == protocol::FrameType::kEvt && frame.msgid == kMsgTextReport) {
            // Route to control queue; UI/log layer decides how to display it.
            response_q->push(protocol::Frame(frame));
        } else {
            // Control frames (RSP, CFG_REPORT, etc.)
            response_q->push(protocol::Frame(frame));
        }
    }

  private:
    static uint64_t now_steady_us() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    }
};
}

void ReadThread::run() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
    if (core_id_ >= 0 && rt_prio_ >= 0) {
        ThreadAffinity::SetRealtimeWithAffinity(core_id_, rt_prio_);
    } else if (core_id_ >= 0) {
        ThreadAffinity::SetCpuAffinity(core_id_);
    } else if (rt_prio_ >= 0) {
        ThreadAffinity::SetRealtimePriority(rt_prio_);
    }
#endif
    ParserState state(response_q_, sample_q_, stats_);

    try {
        serial_->restart_io();
        serial_->start_async_read(
            [&state](const uint8_t *data, size_t length) {
                if (length == 0) {
                    //std::cout << "ReadThread: No data read." << std::endl;
                    return;
                }
                // Set receive time to current time (for real hardware, this is when data arrives)
                state.parser.set_receive_time(ReadThread::now_steady_us());
                state.parser.feed(data, length);
                //std::cout << "ReadThread: readed " << length << " bytes." << std::endl;
            },
            [this](const std::string &error) {
                stats_->io_errors.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "ReadThread: Serial error: " << error << std::endl;
                stop_flag_->store(true);
                serial_->stop_io();
            });
    } catch (const serial::SerialException &e) {
        std::cerr << "ReadThread: Serial error: " << e.what() << std::endl;
        running_ = false;
        return;
    }

    serial_->run_io();
    running_ = false;
}



uint64_t ReadThread::now_steady_us() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace client
}  // namespace powermonitor
