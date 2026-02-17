#include <CLI/CLI.hpp>
#include <CLI/CLI.hpp>
#include <serial/serial.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "power_monitor_session.h"

namespace {

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) { g_stop_requested = true; }

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
    bool usb_stress_mode = false;
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

bool parse_vid_pid(const std::string &hardware_id, uint16_t &vid, uint16_t &pid) {
    std::string upper = hardware_id;
    for (char &c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const std::string key = "USB\\VID_";
    const auto pos = upper.find(key);
    if (pos == std::string::npos || pos + key.size() + 9 > upper.size()) {
        return false;
    }
    const std::string vid_str = upper.substr(pos + key.size(), 4);
    const std::string pid_str = upper.substr(pos + key.size() + 9, 4);
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
        if (stream["usb_stress_mode"] && stream["usb_stress_mode"].IsScalar()) {
            try {
                options.usb_stress_mode = stream["usb_stress_mode"].as<bool>();
            } catch (...) {
            }
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

std::optional<serial::PortInfo> select_port(const ConfigOptions &options) {
    std::vector<serial::PortInfo> ports = serial::list_ports();
    std::vector<serial::PortInfo> matches;
    bool any_parsed = false;
    for (const auto &port : ports) {
        if (port.hardware_id.empty()) {
            continue;
        }
        uint16_t vid = 0;
        uint16_t pid = 0;
        if (!parse_vid_pid(port.hardware_id, vid, pid)) {
            continue;
        }
        any_parsed = true;
        if (vid == options.vid && pid == options.pid) {
            matches.push_back(port);
        }
    }

    if (!options.port.empty()) {
        for (const auto &candidate : matches) {
            if (candidate.port == options.port) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    if (!matches.empty()) {
        return matches.front();
    }

    if (!any_parsed && ports.size() == 1) {
        return ports.front();
    }

    return std::nullopt;
}

bool ensure_output_dir(const std::string &path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            std::cerr << "Failed to create directory '" << p.parent_path().string()
                      << "': " << ec.message() << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

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
    bool usb_stress_mode = options.usb_stress_mode;
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
    app.add_flag("--usb-stress", usb_stress_mode,
                 "Enable device USB throughput stress mode (sets STREAM_START mask bit15)");
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
    if (options.output_path.empty()) {
        options.output_path = default_output_path();
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

    options.usb_stress_mode = usb_stress_mode;

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

    if (!ensure_output_dir(options.output_path)) {
        return 1;
    }

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

    options.port = selected_port->port;

    powermonitor::client::PowerMonitorSession::Options session_options;
    session_options.port = options.port;
    session_options.baud_rate = static_cast<int>(options.baud);
    session_options.output_file = options.output_path;
    session_options.stream_period_us = options.stream_period_us;
    session_options.stream_mask = options.stream_mask;
    session_options.usb_stress_mode = options.usb_stress_mode;
    session_options.verbose = options.verbose;
    session_options.interactive = options.interactive;

    powermonitor::client::PowerMonitorSession session(session_options);
    return session.run();
}
