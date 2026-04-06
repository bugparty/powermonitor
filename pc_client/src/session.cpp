#include "session.h"
#include "onboard_sampler.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>

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

void Session::add_onboard_sample(const OnboardSample& sample) {
    std::lock_guard<std::mutex> lock(onboard_mutex_);
    onboard_samples_.push_back(sample);
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
    j["raw"]["power_u24"] = sample.power_raw;
    j["raw"]["energy_u40"] = sample.energy_raw;
    j["raw"]["charge_s40"] = sample.charge_raw;

    const double vbus_lsb = 195.3125e-6;
    const double vshunt_lsb = config_.adcrange ? 78.125e-9 : 312.5e-9;
    const double current_lsb = config_.current_lsb_nA * 1e-9;
    const double temp_lsb = 7.8125e-3;
    const double power_lsb = current_lsb * 3.2;
    const double energy_lsb = current_lsb * 3.2 * 16.0;
    const double charge_lsb = current_lsb;

    j["engineering"]["vbus_v"] = sample.vbus_raw * vbus_lsb;
    j["engineering"]["vshunt_v"] = sample.vshunt_raw * vshunt_lsb;
    j["engineering"]["current_a"] = sample.current_raw * current_lsb;
    j["engineering"]["temp_c"] = sample.temp_raw * temp_lsb;
    j["engineering"]["power_w"] = sample.power_raw * power_lsb;
    j["engineering"]["energy_j"] = sample.energy_raw * energy_lsb;
    j["engineering"]["charge_c"] = sample.charge_raw * charge_lsb;

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

void Session::save_bundle(const std::string& filepath) const {
    nlohmann::json root;
    root["schema_version"] = "power-bundle/v1";
    root["sources"] = nlohmann::json::object();

    // Add Pico source
    nlohmann::json pico_source;
    pico_source["format"] = "powermonitor_json/v1";
    pico_source["enabled"] = true;
    pico_source["meta"] = build_meta_json();

    // Compute Pico summary
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!samples_.empty()) {
            std::vector<double> powers_w;
            for (const auto& s : samples_) {
                if (s.contains("engineering") && s["engineering"].contains("power_w")) {
                    powers_w.push_back(s["engineering"]["power_w"].get<double>());
                }
            }

            if (!powers_w.empty()) {
                std::sort(powers_w.begin(), powers_w.end());
                double sum = 0.0;
                for (double p : powers_w) sum += p;

                pico_source["summary"]["sample_count"] = powers_w.size();
                pico_source["summary"]["mean_w"] = sum / powers_w.size();
                pico_source["summary"]["p50_w"] = powers_w[powers_w.size() / 2];
                pico_source["summary"]["p95_w"] = powers_w[static_cast<size_t>(0.95 * (powers_w.size() - 1))];

                // Energy (assume 1kHz sampling)
                double duration_s = powers_w.size() / 1000.0;
                pico_source["summary"]["energy_j"] = (sum / powers_w.size()) * duration_s;
            }
        }

        pico_source["samples"] = nlohmann::json::array();
        for (const auto& sample : samples_) {
            pico_source["samples"].push_back(sample);
        }
    }

    root["sources"]["pico"] = pico_source;

    // Add onboard source
    root["sources"]["onboard_cpp"] = build_onboard_source_json();

    // Write to file
    std::ofstream file(filepath);
    if (!file) {
        throw std::runtime_error("Failed to open output file: " + filepath);
    }

    file << std::setw(2) << root << std::endl;
}

nlohmann::json Session::build_onboard_source_json() const {
    nlohmann::json onboard_source;
    onboard_source["format"] = "onboard_csv/v1";
    onboard_source["enabled"] = true;

    onboard_source["meta"]["source"] = onboard_meta_.source;
    onboard_source["meta"]["hwmon_path"] = onboard_meta_.hwmon_path;
    onboard_source["meta"]["columns"] = onboard_meta_.columns;

    // Compute summary
    OnboardSummary summary = compute_onboard_summary();
    onboard_source["summary"]["sample_count"] = summary.sample_count;
    onboard_source["summary"]["mean_w"] = summary.mean_w;
    onboard_source["summary"]["p50_w"] = summary.p50_w;
    onboard_source["summary"]["p95_w"] = summary.p95_w;
    onboard_source["summary"]["energy_j"] = summary.energy_j;

    // Add samples
    onboard_source["samples"] = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(onboard_mutex_);
        for (const auto& sample : onboard_samples_) {
            nlohmann::json j;
            j["mono_ns"] = sample.mono_ns;
            j["unix_ns"] = sample.unix_ns;
            j["rails"]["vdd_in_w"] = sample.vdd_in_mw / 1000.0;
            j["rails"]["vdd_cpu_gpu_cv_w"] = sample.vdd_cpu_gpu_cv_mw / 1000.0;
            j["rails"]["vdd_soc_w"] = sample.vdd_soc_mw / 1000.0;
            j["power_w"] = sample.total_mw / 1000.0;
            j["freqs"]["gpu_hz"] = sample.gpu_freq_hz;
            j["freqs"]["cpu_cluster0_hz"] = sample.cpu_cluster0_freq_hz;
            j["freqs"]["cpu_cluster1_hz"] = sample.cpu_cluster1_freq_hz;
            j["freqs"]["emc_hz"] = sample.emc_freq_hz;
            j["temps"]["cpu_mc"] = sample.temp_cpu_mc;
            j["temps"]["gpu_mc"] = sample.temp_gpu_mc;
            j["temps"]["soc0_mc"] = sample.temp_soc0_mc;
            j["temps"]["soc1_mc"] = sample.temp_soc1_mc;
            j["temps"]["soc2_mc"] = sample.temp_soc2_mc;
            j["temps"]["tj_mc"] = sample.temp_tj_mc;
            j["fan_rpm"] = sample.fan_rpm;
            onboard_source["samples"].push_back(j);
        }
    }

    return onboard_source;
}

Session::OnboardSummary Session::compute_onboard_summary() const {
    OnboardSummary summary;

    std::lock_guard<std::mutex> lock(onboard_mutex_);
    if (onboard_samples_.empty()) {
        return summary;
    }

    std::vector<double> powers_w;
    powers_w.reserve(onboard_samples_.size());

    for (const auto& sample : onboard_samples_) {
        powers_w.push_back(sample.total_mw / 1000.0);
    }

    std::sort(powers_w.begin(), powers_w.end());

    double sum = 0.0;
    for (double p : powers_w) {
        sum += p;
    }

    summary.sample_count = powers_w.size();
    summary.mean_w = sum / powers_w.size();
    summary.p50_w = powers_w[powers_w.size() / 2];
    summary.p95_w = powers_w[static_cast<size_t>(0.95 * (powers_w.size() - 1))];

    // Energy: mean power × duration (assume 1kHz sampling)
    double duration_s = powers_w.size() / 1000.0;
    summary.energy_j = summary.mean_w * duration_s;

    return summary;
}

}  // namespace client
}  // namespace powermonitor
