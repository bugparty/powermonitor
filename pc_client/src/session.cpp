#include "session.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace powermonitor {
namespace client {

void Session::add_sample(const Sample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.push_back(sample_to_json(sample));
}

void Session::save(const std::string& filepath) const {
    nlohmann::json root;
    root["meta"] = build_meta_json();
    root["samples"] = nlohmann::json::array();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sample : samples_) {
            root["samples"].push_back(sample);
        }
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
    j["host_timestamp_us"] = sample.host_timestamp_us;
    j["device_timestamp_us"] = sample.device_timestamp_us;
    j["device_timestamp_unix_us"] = sample.device_timestamp_unix_us;  // 新增
    j["flags"] = sample.flags;
    
    j["raw"]["vbus"] = sample.vbus_raw;
    j["raw"]["vshunt"] = sample.vshunt_raw;
    j["raw"]["current"] = sample.current_raw;
    j["raw"]["temp"] = sample.temp_raw;
    
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
    
    meta["device"]["vid"] = "0x2E8A";
    meta["device"]["pid"] = "0x000A";
    
    meta["config"] = {
        {"current_lsb_nA", config_.current_lsb_nA},
        {"adcrange", config_.adcrange},
        {"stream_period_us", config_.stream_period_us},
        {"stream_mask", config_.stream_mask},
        {"shunt_cal", config_.shunt_cal},
        {"config_reg", config_.config_reg},
        {"adc_config_reg", config_.adc_config_reg}
    };
    
    return meta;
}

}  // namespace client
}  // namespace powermonitor
