#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <serial/serial.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "protocol/frame_builder.h"
#include "protocol/parser.h"
#include "protocol/unpack.h"

namespace {

constexpr uint8_t kMsgPing = 0x01;
constexpr uint8_t kMsgSetCfg = 0x10;
constexpr uint8_t kMsgGetCfg = 0x11;
constexpr uint8_t kMsgStreamStart = 0x30;
constexpr uint8_t kMsgStreamStop = 0x31;
constexpr uint8_t kMsgDataSample = 0x80;
constexpr uint8_t kMsgCfgReport = 0x91;
constexpr uint8_t kStatusOk = 0x00;

constexpr uint64_t kCmdTimeoutUs = 200000;
constexpr uint8_t kMaxRetries = 3;
constexpr uint64_t kDataTimeoutUs = 5'000'000;

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) { g_stop_requested = true; }

uint64_t now_steady_us() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::string format_utc_time(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string format_filename_time(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string hex_u16(uint16_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << value;
    return oss.str();
}

void append_u16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8U);
}

uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<uint32_t>(data[offset + 3]) << 24U);
}

bool parse_hex_u16(const std::string &text, uint16_t &value) {
    try {
        size_t idx = 0;
        int base = 16;
        if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
            base = 0;
        }
        unsigned long raw = std::stoul(text, &idx, base);
        if (idx != text.size() || raw > 0xFFFF) {
            return false;
        }
        value = static_cast<uint16_t>(raw);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_yaml_u32(const YAML::Node &node, uint32_t &out) {
    if (!node || !node.IsScalar()) {
        return false;
    }
    try {
        out = static_cast<uint32_t>(std::stoul(node.Scalar(), nullptr, 0));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_yaml_u16(const YAML::Node &node, uint16_t &out) {
    if (!node || !node.IsScalar()) {
        return false;
    }
    try {
        out = static_cast<uint16_t>(std::stoul(node.Scalar(), nullptr, 0));
        return true;
    } catch (...) {
        return false;
    }
}

struct ConfigOptions {
    std::string port;
    uint32_t baud = 115200;
    uint16_t vid = 0x2E8A;
    uint16_t pid = 0x000A;
    uint16_t stream_period_us = 1000;
    uint16_t stream_mask = 0x000F;
    uint16_t config_reg = 0x0000;
    uint16_t adc_config_reg = 0x1000;
    uint16_t shunt_cal = 0x1000;
    uint16_t shunt_tempco = 0x0000;
    bool config_overridden = false;
    bool verbose = false;
    bool interactive = false;
    std::string output_path;
    std::string config_path;
};

struct SampleRecord {
    uint8_t seq = 0;
    uint64_t timestamp_us = 0;
    uint64_t device_timestamp_us = 0;
    uint8_t flags = 0;
    uint32_t vbus_u20 = 0;
    int32_t vshunt_s20 = 0;
    int32_t current_s20 = 0;
    int16_t dietemp_s16 = 0;
    double vbus_v = 0.0;
    double vshunt_v = 0.0;
    double current_a = 0.0;
    double temp_c = 0.0;
};

bool parse_vid_pid(const std::string &hardware_id, uint16_t &vid, uint16_t &pid) {
    std::string upper = hardware_id;
    for (char &c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const std::string key = "VID:PID=";
    const auto pos = upper.find(key);
    if (pos == std::string::npos || pos + key.size() + 9 > upper.size()) {
        return false;
    }
    const std::string vid_str = upper.substr(pos + key.size(), 4);
    const std::string pid_str = upper.substr(pos + key.size() + 5, 4);
    return parse_hex_u16(vid_str, vid) && parse_hex_u16(pid_str, pid);
}

bool load_yaml_config(const std::string &path, ConfigOptions &options) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load YAML config: " << ex.what() << "\n";
        return false;
    }

    if (const auto stream = root["stream"]) {
        uint16_t period = options.stream_period_us;
        uint16_t mask = options.stream_mask;
        if (parse_yaml_u16(stream["period_us"], period)) {
            options.stream_period_us = period;
        }
        if (parse_yaml_u16(stream["mask"], mask)) {
            options.stream_mask = mask;
        }
    }

    if (const auto ina = root["ina228"]) {
        uint16_t value = 0;
        if (parse_yaml_u16(ina["config_reg"], value)) {
            options.config_reg = value;
            options.config_overridden = true;
        }
        if (parse_yaml_u16(ina["adc_config_reg"], value)) {
            options.adc_config_reg = value;
            options.config_overridden = true;
        }
        if (parse_yaml_u16(ina["shunt_cal"], value)) {
            options.shunt_cal = value;
            options.config_overridden = true;
        }
        if (parse_yaml_u16(ina["shunt_tempco"], value)) {
            options.shunt_tempco = value;
            options.config_overridden = true;
        }
    }

    return true;
}

std::string default_output_path() {
    const auto now = std::chrono::system_clock::now();
    const std::string stamp = format_filename_time(now);
    return "./output/powermonitor_" + stamp + ".json";
}

struct PendingCmd {
    uint8_t msgid = 0;
    uint8_t seq = 0;
    uint8_t retries = 0;
    uint64_t deadline_us = 0;
    std::vector<uint8_t> bytes;
};

class ClientSession {
public:
    ClientSession(serial::Serial &serial, const ConfigOptions &options)
        : serial_(serial),
          options_(options),
          parser_([this](const protocol::Frame &frame) { on_frame(frame); }) {}

    void set_start_time(std::chrono::system_clock::time_point start_wall,
                        uint64_t start_steady_us) {
        start_wall_ = start_wall;
        start_steady_us_ = start_steady_us;
    }

    void tick(uint64_t now_us) {
        current_rx_time_us_ = now_us;
        std::vector<uint8_t> buffer(256);
        size_t read_bytes = 0;
        try {
            read_bytes = serial_.read(buffer.data(), buffer.size());
        } catch (const std::exception &ex) {
            throw std::runtime_error(std::string("Serial read failed: ") + ex.what());
        }
        if (read_bytes > 0) {
            buffer.resize(read_bytes);
            parser_.feed(buffer);
        }

        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now_us >= it->second.deadline_us) {
                if (it->second.retries >= kMaxRetries) {
                    ++timeout_count_;
                    failed_seqs_.insert(it->first);
                    if (options_.verbose) {
                        std::cout << "CMD timeout msgid=0x" << std::hex
                                  << static_cast<int>(it->second.msgid) << std::dec << "\n";
                    }
                    it = pending_.erase(it);
                    continue;
                }
                ++retransmit_count_;
                it->second.retries++;
                it->second.deadline_us = now_us + kCmdTimeoutUs;
                write_bytes(it->second.bytes, now_us);
                if (options_.verbose) {
                    std::cout << "CMD retry msgid=0x" << std::hex
                              << static_cast<int>(it->second.msgid) << " seq="
                              << static_cast<int>(it->second.seq) << std::dec << "\n";
                }
            }
            ++it;
        }
    }

    uint8_t send_cmd(uint8_t msgid, const std::vector<uint8_t> &payload, uint64_t now_us) {
        const uint8_t seq = cmd_seq_++;
        auto bytes = protocol::build_frame(protocol::FrameType::kCmd, 0, seq, msgid, payload);
        write_bytes(bytes, now_us);
        PendingCmd pending;
        pending.msgid = msgid;
        pending.seq = seq;
        pending.deadline_us = now_us + kCmdTimeoutUs;
        pending.bytes = bytes;
        pending_[seq] = pending;
        ++tx_counts_[msgid];
        if (options_.verbose) {
            std::cout << "CMD send msgid=0x" << std::hex << static_cast<int>(msgid)
                      << " seq=" << static_cast<int>(seq) << std::dec << "\n";
        }
        return seq;
    }

    bool wait_for_rsp(uint8_t seq, uint64_t timeout_us) {
        const uint64_t deadline = now_steady_us() + timeout_us;
        while (!g_stop_requested) {
            const uint64_t now_us = now_steady_us();
            if (rsp_status_.count(seq)) {
                return rsp_status_[seq] == kStatusOk;
            }
            if (failed_seqs_.count(seq)) {
                return false;
            }
            if (now_us >= deadline) {
                return false;
            }
            tick(now_us);
        }
        return false;
    }

    bool wait_for_cfg_report(uint64_t timeout_us) {
        const uint64_t deadline = now_steady_us() + timeout_us;
        while (!g_stop_requested) {
            if (cfg_report_received_) {
                return true;
            }
            if (now_steady_us() >= deadline) {
                return false;
            }
            tick(now_steady_us());
        }
        return false;
    }

    bool stream_active() const { return streaming_on_; }

    // Thread-safe getters for interactive mode status query
    size_t sample_count_atomic() const { return sample_count_.load(std::memory_order_relaxed); }
    bool stream_active_atomic() const { return stream_active_atomic_.load(std::memory_order_relaxed); }

    void start_streaming(uint64_t now_us) {
        streaming_on_ = true;
        stream_active_atomic_.store(true, std::memory_order_relaxed);
        last_data_rx_us_ = now_us;
    }

    void stop_streaming() {
        streaming_on_ = false;
        stream_active_atomic_.store(false, std::memory_order_relaxed);
    }

    bool data_timeout(uint64_t now_us) const {
        return streaming_on_ && last_data_rx_us_ > 0 &&
               (now_us - last_data_rx_us_) > kDataTimeoutUs;
    }

    void mark_stop_requested() { stop_requested_ = true; }

    bool stop_requested() const { return stop_requested_; }

    uint64_t crc_fail_count() const { return parser_.crc_fail_count(); }
    uint64_t data_drop_count() const { return data_drop_count_; }
    uint64_t timeout_count() const { return timeout_count_; }
    uint64_t retransmit_count() const { return retransmit_count_; }
    const uint64_t *rx_counts() const { return rx_counts_; }
    const uint64_t *tx_counts() const { return tx_counts_; }

    const std::vector<SampleRecord> &samples() const { return samples_; }

    uint32_t current_lsb_nA() const { return current_lsb_nA_; }
    bool adcrange() const { return adcrange_; }
    uint16_t stream_period_us() const { return stream_period_us_; }
    uint16_t stream_mask() const { return stream_mask_; }
    uint16_t shunt_cal() const { return shunt_cal_; }
    uint16_t config_reg() const { return config_reg_; }
    uint16_t adc_config_reg() const { return adc_config_reg_; }

private:
    void write_bytes(const std::vector<uint8_t> &bytes, uint64_t now_us) {
        (void)now_us;
        try {
            serial_.write(bytes);
        } catch (const std::exception &ex) {
            throw std::runtime_error(std::string("Serial write failed: ") + ex.what());
        }
    }

    void on_frame(const protocol::Frame &frame) {
        ++rx_counts_[frame.msgid];
        if (frame.type == protocol::FrameType::kRsp) {
            handle_rsp(frame);
            return;
        }
        if (frame.type == protocol::FrameType::kEvt && frame.msgid == kMsgCfgReport) {
            handle_cfg_report(frame);
            return;
        }
        if (frame.type == protocol::FrameType::kData && frame.msgid == kMsgDataSample) {
            handle_data_sample(frame);
            return;
        }
    }

    void handle_rsp(const protocol::Frame &frame) {
        if (frame.data.size() < 2) {
            return;
        }
        const uint8_t orig_msgid = frame.data[0];
        const uint8_t status = frame.data[1];
        auto it = pending_.find(frame.seq);
        if (it == pending_.end()) {
            return;
        }
        if (it->second.msgid != orig_msgid) {
            return;
        }
        pending_.erase(it);
        rsp_status_[frame.seq] = status;
        if (options_.verbose) {
            std::cout << "RSP msgid=0x" << std::hex << static_cast<int>(orig_msgid)
                      << " status=0x" << static_cast<int>(status) << std::dec << "\n";
        }
    }

    void handle_cfg_report(const protocol::Frame &frame) {
        if (frame.data.size() < 16) {
            return;
        }
        const uint8_t flags = frame.data[1];
        current_lsb_nA_ = read_u32(frame.data, 2);
        shunt_cal_ = read_u16(frame.data, 6);
        config_reg_ = read_u16(frame.data, 8);
        adc_config_reg_ = read_u16(frame.data, 10);
        stream_period_us_ = read_u16(frame.data, 12);
        stream_mask_ = read_u16(frame.data, 14);
        adcrange_ = (flags & 0x04) != 0;
        cfg_report_received_ = true;
        if (options_.verbose) {
            std::cout << "CFG_REPORT lsb_nA=" << current_lsb_nA_
                      << " shunt_cal=" << shunt_cal_ << " config=0x" << std::hex
                      << config_reg_ << " adc=0x" << adc_config_reg_
                      << " period_us=" << std::dec << stream_period_us_ << " mask=0x"
                      << std::hex << stream_mask_ << std::dec << "\n";
        }
    }

    uint64_t process_device_timestamp(uint32_t ts_us) {
        if (!device_ts_initialized_) {
            device_ts_initialized_ = true;
            last_device_ts_ = ts_us;
        } else if (ts_us < last_device_ts_) {
            ++device_ts_overflow_;
            last_device_ts_ = ts_us;
        } else {
            last_device_ts_ = ts_us;
        }
        return (device_ts_overflow_ << 32U) | ts_us;
    }

    void handle_data_sample(const protocol::Frame &frame) {
        if (frame.data.size() < 16) {
            return;
        }
        if (!has_data_seq_) {
            has_data_seq_ = true;
            last_data_seq_ = frame.seq;
        } else {
            const uint8_t expected = static_cast<uint8_t>(last_data_seq_ + 1);
            if (frame.seq != expected) {
                ++data_drop_count_;
            }
            last_data_seq_ = frame.seq;
        }

        const uint32_t timestamp = read_u32(frame.data, 0);
        const uint8_t flags = frame.data[4];
        const uint8_t *vbus20 = &frame.data[5];
        const uint8_t *vshunt20 = &frame.data[8];
        const uint8_t *current20 = &frame.data[11];
        const int16_t temp_raw = static_cast<int16_t>(read_u16(frame.data, 14));

        const uint32_t vbus_raw = protocol::unpack_u20(vbus20);
        const int32_t vshunt_raw = protocol::unpack_s20(vshunt20);
        const int32_t current_raw = protocol::unpack_s20(current20);
        auto sample = protocol::to_engineering(vbus_raw, vshunt_raw, current_raw, temp_raw,
                                               current_lsb_nA_, adcrange_);

        SampleRecord record;
        record.seq = frame.seq;
        record.timestamp_us = current_rx_time_us_ - start_steady_us_;
        record.device_timestamp_us = process_device_timestamp(timestamp);
        record.flags = flags;
        record.vbus_u20 = vbus_raw;
        record.vshunt_s20 = vshunt_raw;
        record.current_s20 = current_raw;
        record.dietemp_s16 = temp_raw;
        record.vbus_v = sample.vbus_v;
        record.vshunt_v = sample.vshunt_v;
        record.current_a = sample.current_a;
        record.temp_c = sample.temp_c;
        samples_.push_back(record);
        sample_count_.fetch_add(1, std::memory_order_relaxed);

        last_data_rx_us_ = current_rx_time_us_;
    }

    serial::Serial &serial_;
    const ConfigOptions &options_;
    protocol::Parser parser_;
    uint8_t cmd_seq_ = 0;
    std::map<uint8_t, PendingCmd> pending_;
    std::map<uint8_t, uint8_t> rsp_status_;
    std::set<uint8_t> failed_seqs_;
    uint64_t rx_counts_[256] = {};
    uint64_t tx_counts_[256] = {};
    uint32_t current_lsb_nA_ = 1000;
    bool adcrange_ = false;
    uint16_t stream_period_us_ = 0;
    uint16_t stream_mask_ = 0;
    uint16_t shunt_cal_ = 0;
    uint16_t config_reg_ = 0;
    uint16_t adc_config_reg_ = 0;
    bool cfg_report_received_ = false;
    bool streaming_on_ = false;
    std::atomic<size_t> sample_count_{0};
    std::atomic<bool> stream_active_atomic_{false};
    bool stop_requested_ = false;
    bool has_data_seq_ = false;
    uint8_t last_data_seq_ = 0;
    uint64_t data_drop_count_ = 0;
    uint64_t timeout_count_ = 0;
    uint64_t retransmit_count_ = 0;
    uint64_t current_rx_time_us_ = 0;
    uint64_t start_steady_us_ = 0;
    std::chrono::system_clock::time_point start_wall_{};
    uint64_t last_data_rx_us_ = 0;
    bool device_ts_initialized_ = false;
    uint64_t device_ts_overflow_ = 0;
    uint32_t last_device_ts_ = 0;
    std::vector<SampleRecord> samples_;
};

std::optional<serial::PortInfo> select_port(const ConfigOptions &options) {
    const auto ports = serial::list_ports();
    if (!options.port.empty()) {
        for (const auto &port : ports) {
            if (port.port == options.port) {
                return port;
            }
        }
        return std::nullopt;
    }

    std::vector<serial::PortInfo> matches;
    bool any_parsed = false;
    for (const auto &port : ports) {
        uint16_t vid = 0;
        uint16_t pid = 0;
        if (parse_vid_pid(port.hardware_id, vid, pid)) {
            any_parsed = true;
            if (pid == options.pid && (options.vid == 0 || vid == options.vid)) {
                matches.push_back(port);
            }
        }
    }

    if (!matches.empty()) {
        return matches.front();
    }

    if (!any_parsed && ports.size() == 1) {
        return ports.front();
    }

    return std::nullopt;
}

void ensure_output_dir(const std::string &path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
}

nlohmann::json sample_to_json(const SampleRecord &sample) {
    nlohmann::json out;
    out["seq"] = sample.seq;
    out["timestamp_us"] = sample.timestamp_us;
    out["device_timestamp_us"] = sample.device_timestamp_us;
    out["flags"] = sample.flags;
    out["raw"] = {
        {"vbus_u20", sample.vbus_u20},
        {"vshunt_s20", sample.vshunt_s20},
        {"current_s20", sample.current_s20},
        {"dietemp_s16", sample.dietemp_s16},
    };
    out["engineering"] = {
        {"vbus_v", sample.vbus_v},
        {"vshunt_v", sample.vshunt_v},
        {"current_a", sample.current_a},
        {"temp_c", sample.temp_c},
    };
    return out;
}

} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, handle_signal);

    ConfigOptions options;
    options.output_path = default_output_path();

    CLI::App app{"Powermonitor PC client"};
    std::string config_path;
    std::string output_path;
    std::string port_override;
    std::string vid_hex;
    std::string pid_hex;
    uint32_t baud = options.baud;
    uint16_t period = options.stream_period_us;
    uint16_t mask = options.stream_mask;
    uint16_t config_reg = options.config_reg;
    uint16_t adc_config_reg = options.adc_config_reg;
    uint16_t shunt_cal = options.shunt_cal;
    uint16_t shunt_tempco = options.shunt_tempco;

    auto opt_output = app.add_option("-o,--output", output_path, "Output JSON file path");
    auto opt_config = app.add_option("-c,--config", config_path, "YAML configuration file");
    auto opt_port = app.add_option("-p,--port", port_override, "Serial port");
    auto opt_baud = app.add_option("-b,--baud", baud, "Baud rate");
    auto opt_period = app.add_option("--period", period, "Sampling period in microseconds");
    auto opt_mask = app.add_option("--mask", mask, "Channel mask");
    auto opt_vid = app.add_option("--vid", vid_hex, "USB VID (hex)");
    auto opt_pid = app.add_option("--pid", pid_hex, "USB PID (hex)");
    auto opt_config_reg = app.add_option("--config-reg", config_reg, "INA228 config register");
    auto opt_adc_reg = app.add_option("--adc-config-reg", adc_config_reg, "INA228 ADC config");
    auto opt_shunt_cal = app.add_option("--shunt-cal", shunt_cal, "INA228 shunt calibration");
    auto opt_shunt_tempco =
        app.add_option("--shunt-tempco", shunt_tempco, "INA228 shunt tempco");
    app.add_flag("-v,--verbose", options.verbose, "Verbose logging");
    app.add_flag("-i,--interactive", options.interactive, "Interactive mode");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (opt_config->count() > 0) {
        if (!load_yaml_config(config_path, options)) {
            return 1;
        }
    }

    if (opt_output->count() > 0) {
        options.output_path = output_path;
    }

    if (opt_port->count() > 0) {
        options.port = port_override;
    }

    if (opt_baud->count() > 0) {
        options.baud = baud;
    }

    if (opt_period->count() > 0) {
        options.stream_period_us = period;
    }

    if (opt_mask->count() > 0) {
        options.stream_mask = mask;
    }

    if (opt_vid->count() > 0) {
        if (!parse_hex_u16(vid_hex, options.vid)) {
            std::cerr << "Invalid VID value\n";
            return 1;
        }
    }

    if (opt_pid->count() > 0) {
        if (!parse_hex_u16(pid_hex, options.pid)) {
            std::cerr << "Invalid PID value\n";
            return 1;
        }
    }

    if (opt_config_reg->count() > 0) {
        options.config_reg = config_reg;
        options.config_overridden = true;
    }
    if (opt_adc_reg->count() > 0) {
        options.adc_config_reg = adc_config_reg;
        options.config_overridden = true;
    }
    if (opt_shunt_cal->count() > 0) {
        options.shunt_cal = shunt_cal;
        options.config_overridden = true;
    }
    if (opt_shunt_tempco->count() > 0) {
        options.shunt_tempco = shunt_tempco;
        options.config_overridden = true;
    }

    ensure_output_dir(options.output_path);

    serial::Serial serial;
    serial.setBaudrate(options.baud);
    serial::Timeout timeout = serial::Timeout::simpleTimeout(50);
    serial.setTimeout(timeout);

    std::optional<serial::PortInfo> selected_port;
    int wait_count = 0;
    while (!g_stop_requested) {
        selected_port = select_port(options);
        if (selected_port.has_value() && !selected_port->port.empty()) {
            if (wait_count > 0) {
                std::cout << "\n";
            }
            break;
        }
        if (!options.port.empty()) {
            std::cerr << "Specified port not found\n";
            return 1;
        }
        const int dots = (wait_count % 3) + 1;
        std::cout << "\rWaiting for device (VID=" << hex_u16(options.vid) 
                  << ", PID=" << hex_u16(options.pid) << ")";
        for (int i = 0; i < dots; ++i) {
            std::cout << ".";
        }
        for (int i = dots; i < 3; ++i) {
            std::cout << " ";
        }
        std::cout << std::flush;
        ++wait_count;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!selected_port.has_value()) {
        std::cerr << "No matching serial port found\n";
        return 1;
    }

    const std::string port_name = selected_port->port;
    uint16_t actual_vid = options.vid;
    uint16_t actual_pid = options.pid;
    if (!selected_port->hardware_id.empty()) {
        parse_vid_pid(selected_port->hardware_id, actual_vid, actual_pid);
    }
    serial.setPort(port_name);

    try {
        serial.open();
    } catch (const std::exception &ex) {
        std::cerr << "Failed to open serial port: " << ex.what() << "\n";
        return 1;
    }

    if (!serial.isOpen()) {
        std::cerr << "Failed to open serial port\n";
        return 1;
    }

    const auto session_start_wall = std::chrono::system_clock::now();
    const uint64_t session_start_steady = now_steady_us();

    ClientSession session(serial, options);
    session.set_start_time(session_start_wall, session_start_steady);

    const uint64_t start_now = now_steady_us();
    const uint8_t ping_seq = session.send_cmd(kMsgPing, {}, start_now);
    if (!session.wait_for_rsp(ping_seq, 2'000'000)) {
        std::cerr << "PING failed\n";
        return 1;
    }

    const uint8_t get_cfg_seq = session.send_cmd(kMsgGetCfg, {}, now_steady_us());
    if (!session.wait_for_rsp(get_cfg_seq, 2'000'000)) {
        std::cerr << "GET_CFG failed\n";
        return 1;
    }
    session.wait_for_cfg_report(2'000'000);

    if (options.config_overridden) {
        std::vector<uint8_t> payload;
        append_u16(payload, options.config_reg);
        append_u16(payload, options.adc_config_reg);
        append_u16(payload, options.shunt_cal);
        append_u16(payload, options.shunt_tempco);
        const uint8_t set_seq = session.send_cmd(kMsgSetCfg, payload, now_steady_us());
        if (!session.wait_for_rsp(set_seq, 2'000'000)) {
            std::cerr << "SET_CFG failed\n";
            return 1;
        }
        session.wait_for_cfg_report(2'000'000);
    }

    std::atomic<bool> interactive_start{!options.interactive};
    std::atomic<bool> interactive_stop{false};
    std::thread input_thread;
    if (options.interactive) {
        input_thread = std::thread([&]() {
            std::string line;
            while (!g_stop_requested && std::getline(std::cin, line)) {
                if (line == "start") {
                    interactive_start = true;
                } else if (line == "stop") {
                    interactive_stop = true;
                    g_stop_requested = true;
                    break;
                } else if (line == "status") {
                    std::cout << "samples=" << session.sample_count_atomic()
                              << " streaming=" << session.stream_active_atomic() << "\n";
                }
            }
        });
    }

    if (options.interactive) {
        std::cout << "Interactive mode: type 'start' to begin, 'stop' to end\n";
        while (!g_stop_requested && !interactive_start.load()) {
            session.tick(now_steady_us());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (!g_stop_requested) {
        std::vector<uint8_t> start_payload;
        append_u16(start_payload, options.stream_period_us);
        append_u16(start_payload, options.stream_mask);
        const uint8_t start_seq =
            session.send_cmd(kMsgStreamStart, start_payload, now_steady_us());
        if (!session.wait_for_rsp(start_seq, 2'000'000)) {
            std::cerr << "STREAM_START failed\n";
            return 1;
        }
        session.start_streaming(now_steady_us());
    }

    while (!g_stop_requested) {
        const uint64_t now_us = now_steady_us();
        session.tick(now_us);
        if (session.data_timeout(now_us)) {
            std::cerr << "No DATA received for 5 seconds, stopping\n";
            break;
        }
        if (interactive_stop.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (input_thread.joinable()) {
        input_thread.join();
    }

    if (session.stream_active()) {
        const uint8_t stop_seq = session.send_cmd(kMsgStreamStop, {}, now_steady_us());
        session.wait_for_rsp(stop_seq, 500'000);
        session.stop_streaming();
    }

    const auto session_end_wall = std::chrono::system_clock::now();
    const uint64_t session_end_steady = now_steady_us();
    const uint64_t duration_us = session_end_steady - session_start_steady;

    nlohmann::json root;
    nlohmann::json meta;
    meta["schema_version"] = "1.0";
    meta["protocol_version"] = protocol::kProtoVersion;
    meta["device"] = {
        {"vid", hex_u16(actual_vid)},
        {"pid", hex_u16(actual_pid)},
        {"port", port_name},
        {"baud", options.baud},
    };
    meta["session"] = {
        {"start_time_utc", format_utc_time(session_start_wall)},
        {"end_time_utc", format_utc_time(session_end_wall)},
        {"duration_us", duration_us},
    };
    meta["config"] = {
        {"stream_period_us", options.stream_period_us},
        {"stream_mask", options.stream_mask},
        {"current_lsb_nA", session.current_lsb_nA()},
        {"adcrange", session.adcrange() ? 1 : 0},
        {"shunt_cal", session.shunt_cal()},
        {"config_reg", session.config_reg()},
        {"adc_config_reg", session.adc_config_reg()},
    };

    nlohmann::json rx_counts = nlohmann::json::object();
    for (int i = 0; i < 256; ++i) {
        if (session.rx_counts()[i] > 0) {
            std::ostringstream key;
            key << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << i;
            rx_counts[key.str()] = session.rx_counts()[i];
        }
    }
    nlohmann::json stats = {
        {"rx_counts", rx_counts},
        {"crc_fail", session.crc_fail_count()},
        {"data_drop", session.data_drop_count()},
        {"timeouts", session.timeout_count()},
        {"retries", session.retransmit_count()},
    };
    meta["stats"] = stats;

    root["meta"] = meta;

    nlohmann::json samples = nlohmann::json::array();
    for (const auto &sample : session.samples()) {
        samples.push_back(sample_to_json(sample));
    }
    root["samples"] = samples;

    std::ofstream out(options.output_path);
    if (!out) {
        std::cerr << "Failed to open output file: " << options.output_path << "\n";
        return 1;
    }
    out << root.dump(2) << "\n";
    std::cout << "Saved " << session.samples().size() << " samples to "
              << options.output_path << "\n";
    return 0;
}
