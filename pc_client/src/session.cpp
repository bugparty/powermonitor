#include "session.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace powermonitor {
namespace client {

namespace {

std::string format_utc_iso8601(uint64_t unix_us) {
    if (unix_us == 0) {
        return "";
    }
    const std::time_t sec = static_cast<std::time_t>(unix_us / 1000000ULL);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &sec);
#else
    gmtime_r(&sec, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

nlohmann::json counts_to_json(const std::array<uint64_t, 256>& counts) {
    nlohmann::json j = nlohmann::json::object();
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] == 0) {
            continue;
        }
        std::ostringstream key;
        key << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << i;
        j[key.str()] = counts[i];
    }
    return j;
}

}  // namespace

void Session::add_sample(const Sample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.push_back(sample_to_json(sample));
}

void Session::save(const std::string& filepath) const {
    nlohmann::json root;
    root["meta"] = build_meta_json();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root["samples"] = samples_;
    }
    
    std::ofstream file(filepath);
    if (!file) {
        throw std::runtime_error("Failed to open output file: " + filepath);
    }
    
    file << std::setw(2) << root << std::endl;
}

uint64_t Session::process_device_timestamp(uint32_t ts_us) {
    if (ts_us < last_device_ts_ && last_device_ts_ - ts_us > 0x80000000) {
        // 检测到回绕
        overflow_count_++;
    }
    last_device_ts_ = ts_us;
    return (overflow_count_ << 32) | ts_us;
}

nlohmann::json Session::sample_to_json(const Sample& sample) const {
    nlohmann::json j;
    j["seq"] = sample.seq;
    j["timestamp_us"] = sample.host_timestamp_us;
    j["host_timestamp_us"] = sample.host_timestamp_us;
    j["device_timestamp_us"] = sample.device_timestamp_us;
    j["device_timestamp_unix_us"] = sample.device_timestamp_unix_us;
    j["flags"] = sample.flags;

    j["raw"]["vbus_u20"] = sample.vbus_raw;
    j["raw"]["vshunt_s20"] = sample.vshunt_raw;
    j["raw"]["current_s20"] = sample.current_raw;
    j["raw"]["dietemp_s16"] = sample.temp_raw;

    const double vbus_lsb = 195.3125e-6;
    const double vshunt_lsb = config_.adcrange ? 78.125e-9 : 312.5e-9;
    const double current_lsb = config_.current_lsb_nA * 1e-9;
    const double temp_lsb = 7.8125e-3;

    j["engineering"]["vbus_v"] = sample.vbus_raw * vbus_lsb;
    j["engineering"]["vshunt_v"] = sample.vshunt_raw * vshunt_lsb;
    j["engineering"]["current_a"] = sample.current_raw * current_lsb;
    j["engineering"]["temp_c"] = sample.temp_raw * temp_lsb;

    return j;
}

nlohmann::json Session::build_meta_json() const {
    nlohmann::json meta;
    meta["schema_version"] = "1.0";
    meta["protocol_version"] = 1;

    meta["device"]["vid"] = runtime_meta_.vid_hex;
    meta["device"]["pid"] = runtime_meta_.pid_hex;
    meta["device"]["port"] = runtime_meta_.port;
    meta["device"]["baud"] = runtime_meta_.baud;

    meta["session"]["start_time_utc"] = format_utc_iso8601(session_start_unix_us_);
    meta["session"]["end_time_utc"] = format_utc_iso8601(session_end_unix_us_);
    meta["session"]["duration_us"] =
        session_end_unix_us_ >= session_start_unix_us_ ? (session_end_unix_us_ - session_start_unix_us_) : 0;

    meta["run"]["label"] = runtime_meta_.run_label;
    meta["run"]["tags"] = runtime_meta_.tags;

    meta["config"] = {
        {"current_lsb_nA", config_.current_lsb_nA},
        {"adcrange", config_.adcrange},
        {"stream_period_us", config_.stream_period_us},
        {"stream_mask", config_.stream_mask},
        {"shunt_cal", config_.shunt_cal},
        {"config_reg", config_.config_reg},
        {"adc_config_reg", config_.adc_config_reg}
    };

    meta["stats"]["rx_counts"] = counts_to_json(stats_.rx_counts);
    meta["stats"]["tx_counts"] = counts_to_json(stats_.tx_counts);
    meta["stats"]["crc_fail"] = stats_.crc_fail;
    meta["stats"]["data_drop"] = stats_.data_drop;
    meta["stats"]["queue_overflow"] = stats_.queue_overflow;
    meta["stats"]["timeouts"] = stats_.timeouts;
    meta["stats"]["retries"] = stats_.retries;
    meta["stats"]["io_errors"] = stats_.io_errors;

    return meta;
}

}  // namespace client
}  // namespace powermonitor
