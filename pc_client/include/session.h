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

/**
 * @brief Sample structure for onboard power measurements from Jetson Nano hwmon
 */
struct OnboardSample {
    int64_t mono_ns = 0;
    int64_t unix_ns = 0;

    // INA3221 power rails (mW)
    int64_t vdd_in_mw = 0;
    int64_t vdd_cpu_gpu_cv_mw = 0;
    int64_t vdd_soc_mw = 0;
    int64_t total_mw = 0;

    // GPU / CPU / EMC frequencies (Hz, -1 = unavailable)
    int64_t gpu_freq_hz = -1;
    int64_t cpu_cluster0_freq_hz = -1;
    int64_t cpu_cluster1_freq_hz = -1;
    int64_t emc_freq_hz = -1;

    // Temperatures (milli-Celsius, -1 = unavailable)
    int64_t temp_cpu_mc = -1;
    int64_t temp_gpu_mc = -1;
    int64_t temp_soc0_mc = -1;
    int64_t temp_soc1_mc = -1;
    int64_t temp_soc2_mc = -1;
    int64_t temp_tj_mc = -1;

    // Fan (RPM, -1 = unavailable)
    int64_t fan_rpm = -1;
};

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
        uint32_t power_raw = 0;
        int16_t temp_raw = 0;
        uint64_t energy_raw = 0;
        int64_t charge_raw = 0;
    };

    struct RuntimeMeta {
        std::string vid_hex = "0x0000";
        std::string pid_hex = "0x0000";
        std::string port;
        uint32_t baud = 0;
        std::string run_label;
        std::vector<std::string> tags;
    };

    // Onboard power data structures
    struct OnboardMeta {
        std::string hwmon_path;
        std::string source = "onboard_cpp";
        std::string columns = "mono_ns,unix_ns,vdd_in_mw,vdd_cpu_gpu_cv_mw,vdd_soc_mw,total_mw,"
                             "gpu_freq_hz,cpu_cluster0_freq_hz,cpu_cluster1_freq_hz,emc_freq_hz,"
                             "temp_cpu_mc,temp_gpu_mc,temp_soc0_mc,temp_soc1_mc,temp_soc2_mc,temp_tj_mc,"
                             "fan_rpm";
    };

    struct OnboardSummary {
        size_t sample_count = 0;
        double mean_w = 0.0;
        double p50_w = 0.0;
        double p95_w = 0.0;
        double energy_j = 0.0;
    };

    Session() = default;
    ~Session() = default;

    void set_config(const Config& cfg) { config_ = cfg; }
    const Config& get_config() const { return config_; }

    void add_sample(const Sample& sample);
    void save(const std::string& filepath) const;
    void save_merged(const std::string& filepath) const { save(filepath); }
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

    // Onboard power data methods
    void set_onboard_meta(const OnboardMeta& meta) { onboard_meta_ = meta; }
    void add_onboard_sample(const OnboardSample& sample);
    size_t onboard_sample_count() const {
        std::lock_guard<std::mutex> lock(onboard_mutex_);
        return onboard_samples_.size();
    }
    void save_bundle(const std::string& filepath) const;
    void save_bundle_merged(const std::string& filepath) const { save_bundle(filepath); }

    // Flush interface for periodic data dumps
    void set_flush_dir(const std::string& dir) { flush_dir_ = dir; }
    bool flush_to_chunks();
    size_t total_pico_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }

private:
    Config config_;
    RuntimeMeta runtime_meta_;
    Stats stats_;
    uint64_t session_start_unix_us_ = 0;
    uint64_t session_end_unix_us_ = 0;
    std::vector<nlohmann::json> samples_;
    mutable std::mutex mutex_;

    // Onboard power data
    OnboardMeta onboard_meta_;
    std::vector<OnboardSample> onboard_samples_;
    mutable std::mutex onboard_mutex_;

    // Flush interface
    std::string flush_dir_;

    nlohmann::json sample_to_json(const Sample& sample) const;
    nlohmann::json build_meta_json() const;
    nlohmann::json build_onboard_source_json() const;
    OnboardSummary compute_onboard_summary() const;
};

}  // namespace client
}  // namespace powermonitor
