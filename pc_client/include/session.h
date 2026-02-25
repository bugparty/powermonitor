#pragma once

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

    // 时间戳溢出处理
    uint64_t process_device_timestamp(uint32_t ts_us);

private:
    Config config_;
    std::vector<nlohmann::json> samples_;
    mutable std::mutex mutex_;

    // 时间戳溢出状态
    uint32_t last_device_ts_ = 0;
    uint64_t overflow_count_ = 0;

    nlohmann::json sample_to_json(const Sample& sample) const;
    nlohmann::json build_meta_json() const;
};

}  // namespace client
}  // namespace powermonitor
