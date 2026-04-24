#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace powermonitor {
namespace client {

class Session {
public:
    struct Stats {
        std::array<uint64_t, 256> rx_counts{};
        std::array<uint64_t, 256> tx_counts{};
        uint64_t crc_fail = 0;
        uint64_t data_drop = 0;
        uint64_t queue_overflow = 0;
        uint64_t timeouts = 0;
        uint64_t retries = 0;
        uint64_t io_errors = 0;
    };

    struct Config {
        uint32_t current_lsb_nA = 0;
        uint8_t adcrange = 0;
        uint16_t stream_period_us = 1000;
        uint16_t stream_mask = 0x000F;
        uint16_t shunt_cal = 0;
        uint16_t config_reg = 0;
        uint16_t adc_config_reg = 0;
    };

    struct Sample {
        uint32_t seq = 0;
        uint64_t host_timestamp_us = 0;
        uint64_t device_timestamp_us = 0;
        uint64_t device_timestamp_unix_us = 0;  // Absolute timestamp in microseconds since Unix epoch
        uint8_t flags = 0;
        uint32_t vbus_raw = 0;
        int32_t vshunt_raw = 0;
        int32_t current_raw = 0;
        int16_t temp_raw = 0;
    };

    struct RuntimeMeta {
        std::string vid_hex = "0x0000";
        std::string pid_hex = "0x0000";
        std::string port;
        uint32_t baud = 0;
        std::string run_label;
        std::vector<std::string> tags;
    };

    Session() = default;
    ~Session() = default;

    void set_config(const Config& cfg) { config_ = cfg; }
    const Config& get_config() const { return config_; }

    void add_sample(const Sample& sample);
    void save(const std::string& filepath) const;
    size_t sample_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }

    void set_runtime_meta(const RuntimeMeta& meta) { runtime_meta_ = meta; }
    void set_stats(const Stats& stats) { stats_ = stats; }
    void set_session_timing(uint64_t start_unix_us, uint64_t end_unix_us) {
        session_start_unix_us_ = start_unix_us;
        session_end_unix_us_ = end_unix_us;
    }

    // Handle device timestamp overflow
    uint64_t process_device_timestamp(uint32_t ts_us);

private:
    Config config_;
    RuntimeMeta runtime_meta_;
    Stats stats_;
    uint64_t session_start_unix_us_ = 0;
    uint64_t session_end_unix_us_ = 0;
    std::vector<nlohmann::json> samples_;
    mutable std::mutex mutex_;

    // Device timestamp overflow state
    uint32_t last_device_ts_ = 0;
    uint64_t overflow_count_ = 0;

    nlohmann::json sample_to_json(const Sample& sample) const;
    nlohmann::json build_meta_json() const;
};

}  // namespace client
}  // namespace powermonitor
