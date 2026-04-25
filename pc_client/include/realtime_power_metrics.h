#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace powermonitor {
namespace client {

constexpr const char* POWER_METRICS_SHM_NAME = "/powermonitor_power_metrics";
constexpr uint32_t kPowerMetricsVersion = 1;
constexpr size_t kPowerMetricsRingCapacity = 4096;

enum class PowerSampleSource : uint32_t {
    kUnknown = 0,
    kPico = 1,
    kOnboard = 2,
};

struct RealtimePowerSample {
    uint64_t sequence_num;

    uint32_t source;
    uint32_t flags;

    uint64_t host_timestamp_us;
    uint64_t unix_timestamp_us;
    uint64_t device_timestamp_us;
    uint64_t device_timestamp_unix_us;

    double power_w;
    double voltage_v;
    double current_a;
    double temp_c;

    uint64_t gpu_freq_hz;
    uint64_t cpu_cluster0_freq_hz;
    uint64_t cpu_cluster1_freq_hz;
    uint64_t emc_freq_hz;

    double cpu_temp_c;
    double gpu_temp_c;

    uint64_t reserved0;
    uint64_t reserved1;
};

struct RealtimePowerRingSlot {
    std::atomic<uint64_t> sequence_guard;
    RealtimePowerSample sample;
};

struct RealtimePowerRingHeader {
    uint32_t version;
    uint32_t capacity;
    std::atomic<uint64_t> write_index;
    std::atomic<uint64_t> dropped_records;
    std::atomic<uint64_t> writer_pid;
};

struct RealtimePowerMetricsRegion {
    RealtimePowerRingHeader header;
    std::array<RealtimePowerRingSlot, kPowerMetricsRingCapacity> slots;
};

}  // namespace client
}  // namespace powermonitor
