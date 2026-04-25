#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "realtime_power_metrics.h"

namespace powermonitor {
namespace client {

class ShmPowerRingBuffer {
public:
    ShmPowerRingBuffer() {
        init();
    }

    ~ShmPowerRingBuffer() {
        cleanup();
    }

    ShmPowerRingBuffer(const ShmPowerRingBuffer&) = delete;
    ShmPowerRingBuffer& operator=(const ShmPowerRingBuffer&) = delete;

    bool ok() const {
        return region_ != nullptr;
    }

    void push(const RealtimePowerSample& input_sample) {
        if (!region_) {
            return;
        }

        const uint64_t write_index = region_->header.write_index.fetch_add(1, std::memory_order_acq_rel);
        RealtimePowerRingSlot& slot = region_->slots[write_index % region_->header.capacity];

        const uint64_t guard_begin = (write_index << 1U) | 1U;
        const uint64_t guard_end = (write_index << 1U);

        slot.sequence_guard.store(guard_begin, std::memory_order_release);

        RealtimePowerSample sample = input_sample;
        sample.sequence_num = write_index;
        slot.sample = sample;

        slot.sequence_guard.store(guard_end, std::memory_order_release);
    }

private:
    void init() {
        fd_ = shm_open(POWER_METRICS_SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) {
            log_warn("shm_open failed");
            return;
        }

        const size_t region_size = sizeof(RealtimePowerMetricsRegion);
        if (ftruncate(fd_, static_cast<off_t>(region_size)) != 0) {
            log_warn("ftruncate failed");
            cleanup();
            return;
        }

        void* mapped = mmap(nullptr, region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped == MAP_FAILED) {
            log_warn("mmap failed");
            cleanup();
            return;
        }

        region_ = static_cast<RealtimePowerMetricsRegion*>(mapped);
        initialize_header_if_needed();
    }

    void initialize_header_if_needed() {
        if (!region_) {
            return;
        }

        if (region_->header.version != kPowerMetricsVersion ||
            region_->header.capacity != kPowerMetricsRingCapacity) {
            std::memset(region_, 0, sizeof(RealtimePowerMetricsRegion));
            region_->header.version = kPowerMetricsVersion;
            region_->header.capacity = kPowerMetricsRingCapacity;
            region_->header.write_index.store(0, std::memory_order_relaxed);
            region_->header.dropped_records.store(0, std::memory_order_relaxed);
        }

        region_->header.writer_pid.store(static_cast<uint64_t>(::getpid()), std::memory_order_relaxed);
    }

    void cleanup() {
        if (region_) {
            munmap(region_, sizeof(RealtimePowerMetricsRegion));
            region_ = nullptr;
        }
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    void log_warn(const char* message) {
        std::fprintf(stderr, "[ShmPowerRingBuffer] %s: %s\n", message, std::strerror(errno));
    }

    int fd_ = -1;
    RealtimePowerMetricsRegion* region_ = nullptr;
};

}  // namespace client
}  // namespace powermonitor
