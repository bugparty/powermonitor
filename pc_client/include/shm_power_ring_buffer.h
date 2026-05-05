#pragma once

#include <spsc_ring_buffer.hpp>
#include "realtime_power_metrics.h"

namespace powermonitor {
namespace client {

class ShmPowerRingBuffer {
public:
    ShmPowerRingBuffer()
        : buffer_(POWER_METRICS_SHM_NAME,
                  kPowerMetricsVersion,
                  buffers::ShmOpenMode::create_or_open) {}

    explicit ShmPowerRingBuffer(const char* shm_name)
        : buffer_(shm_name,
                  kPowerMetricsVersion,
                  buffers::ShmOpenMode::create_or_open) {}

    void push(const RealtimePowerSample& sample) {
        buffer_.push_overwrite(sample);
    }

    uint64_t overflow_count() const {
        return buffer_.overflow_count();
    }

    bool valid() const { return buffer_.valid(); }
    bool is_creator() const { return buffer_.is_creator(); }

private:
    using PowerSpscBuffer = buffers::spsc_ring_buffer<
        RealtimePowerSample,
        kPowerMetricsRingCapacity,
        buffers::ShmStorage
    >;

    PowerSpscBuffer buffer_;
};

}  // namespace client
}  // namespace powermonitor