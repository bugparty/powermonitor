#include "read_thread.h"

#include "read_thread.h"

#include <chrono>
#include <iostream>
#include <string>

#include <serial/serial.h>

#include "protocol/unpack.h"
#include "protocol_helpers.h"

namespace powermonitor {
namespace client {

ReadThread::ReadThread(serial::Serial* serial, SampleQueue* sample_q, 
                       ResponseQueue* response_q, std::atomic<bool>* stop_flag,
                       ThreadStats* stats)
    : serial_(serial),
      sample_q_(sample_q),
      response_q_(response_q),
      stop_flag_(stop_flag),
      stats_(stats) {
}

ReadThread::~ReadThread() {
    if (running_.load()) {
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
struct ParserState {
    protocol::Parser parser;
    ResponseQueue* response_q = nullptr;
    SampleQueue* sample_q = nullptr;
    ThreadStats* stats = nullptr;
    
    ParserState(ResponseQueue* rq, SampleQueue* sq, ThreadStats* st)
        : parser([this](const protocol::Frame& frame) {
            this->on_frame(frame);
          }),
          response_q(rq), sample_q(sq), stats(st) {}
    
    void on_frame(const protocol::Frame& frame) {
        stats->rx_counts[frame.msgid].fetch_add(1, std::memory_order_relaxed);
        
        if (frame.msgid == 0x80) {  // DATA_SAMPLE
            if (frame.data.size() < 16) {
                stats->crc_fail.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            
            SampleQueue::Sample sample;
            sample.seq = frame.seq;
            sample.host_timestamp_us = now_steady_us();
            sample.device_timestamp_us = unpack_u32(frame.data.data());
            sample.raw_data = frame.data;
            
            sample_q->push(std::move(sample));
        } else {
            // 控制帧（RSP, CFG_REPORT等）
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
    ParserState state(response_q_, sample_q_, stats_);

    try {
        serial_->restart_io();
        serial_->start_async_read(
            [&state](const uint8_t *data, size_t length) {
                if (length == 0) {
                    //std::cout << "ReadThread: No data read." << std::endl;
                    return;
                }
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
