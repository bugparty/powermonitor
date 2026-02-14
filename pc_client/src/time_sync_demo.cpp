#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "protocol/frame_builder.h"
#include "protocol/parser.h"
#include "serial/serial.h"

namespace {

constexpr uint8_t kMsgTimeSync = 0x05;
constexpr uint8_t kMsgTimeAdjust = 0x06;
constexpr uint8_t kMsgTimeSet = 0x07;

struct DemoOptions {
    std::string port;
    uint32_t baud = 115200;
    uint16_t vid = 0x2E8A;
    uint16_t pid = 0x000A;
    int sync_count = 10;
    int interval_ms = 1000;
    int cmd_timeout_ms = 1000;
    bool do_time_set = true;
    int drift_interval_ms = 100;  // New: interval for drift test
    bool run_drift_test = false; // New: run continuous drift test
};

struct TimedFrame {
    protocol::Frame frame;
    uint64_t receive_time_us = 0;
};

uint64_t now_steady_us() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

uint64_t now_unix_us() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void pack_u64_le(std::vector<uint8_t> &dst, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void pack_s64_le(std::vector<uint8_t> &dst, int64_t value) {
    pack_u64_le(dst, static_cast<uint64_t>(value));
}

uint64_t read_u64_le(const std::vector<uint8_t> &buf, size_t offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(buf[offset + static_cast<size_t>(i)]) << (i * 8);
    }
    return value;
}

int64_t signed_diff_u64(uint64_t a, uint64_t b) {
    if (a >= b) {
        return static_cast<int64_t>(a - b);
    }
    return -static_cast<int64_t>(b - a);
}

bool parse_bool(const std::string &value, bool *out) {
    if (value == "1" || value == "true" || value == "TRUE" || value == "True") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" || value == "False") {
        *out = false;
        return true;
    }
    return false;
}

bool parse_hex_u16(const std::string &text, uint16_t *value) {
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
        *value = static_cast<uint16_t>(raw);
        return true;
    } catch (...) {
        return false;
    }
}

bool consume_string_arg(int &i, int argc, char **argv, const std::string &name, std::string *out) {
    const std::string arg(argv[i]);
    if (arg == name) {
        if (i + 1 >= argc) {
            return false;
        }
        *out = argv[++i];
        return true;
    }
    const std::string prefix = name + "=";
    if (arg.rfind(prefix, 0) == 0) {
        *out = arg.substr(prefix.size());
        return true;
    }
    return false;
}

bool consume_int_arg(int &i, int argc, char **argv, const std::string &name, int *out) {
    std::string value;
    if (!consume_string_arg(i, argc, argv, name, &value)) {
        return false;
    }
    *out = std::stoi(value);
    return true;
}

bool consume_uint32_arg(int &i, int argc, char **argv, const std::string &name, uint32_t *out) {
    std::string value;
    if (!consume_string_arg(i, argc, argv, name, &value)) {
        return false;
    }
    *out = static_cast<uint32_t>(std::stoul(value));
    return true;
}

bool consume_bool_arg(int &i, int argc, char **argv, const std::string &name, bool *out) {
    const std::string arg(argv[i]);
    if (arg == name) {
        *out = true;
        return true;
    }
    const std::string prefix = name + "=";
    if (arg.rfind(prefix, 0) == 0) {
        return parse_bool(arg.substr(prefix.size()), out);
    }
    return false;
}

std::string to_upper(std::string input) {
    for (char &c : input) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return input;
}

bool parse_vid_pid(const std::string &hardware_id, uint16_t *vid, uint16_t *pid) {
    const std::string upper = to_upper(hardware_id);
    const std::string win_key = "USB\\VID_";
    const std::string linux_key = "VID:PID=";
    if (upper.rfind(win_key, 0) == 0) {
        if (upper.size() < win_key.size() + 13) {
            return false;
        }
        const std::string vid_str = upper.substr(win_key.size(), 4);
        const std::string pid_str = upper.substr(win_key.size() + 9, 4);
        return parse_hex_u16(vid_str, vid) && parse_hex_u16(pid_str, pid);
    }
    const auto pos = upper.find(linux_key);
    if (pos != std::string::npos && pos + linux_key.size() + 9 <= upper.size()) {
        const std::string vid_str = upper.substr(pos + linux_key.size(), 4);
        const std::string pid_str = upper.substr(pos + linux_key.size() + 5, 4);
        return parse_hex_u16(vid_str, vid) && parse_hex_u16(pid_str, pid);
    }
    return false;
}

std::optional<serial::PortInfo> find_port(const DemoOptions &options) {
    std::vector<serial::PortInfo> ports = serial::list_ports();
    if (!options.port.empty()) {
        for (const auto &port : ports) {
            if (port.port == options.port) {
                return port;
            }
        }
        return std::nullopt;
    }

    for (const auto &port : ports) {
        if (port.hardware_id.empty()) {
            continue;
        }
        uint16_t vid = 0;
        uint16_t pid = 0;
        if (!parse_vid_pid(port.hardware_id, &vid, &pid)) {
            continue;
        }
        if (vid == options.vid && pid == options.pid) {
            return port;
        }
    }
    return std::nullopt;
}

void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --port <name>                Explicit serial port (e.g. COM7)\n"
              << "  --baud <rate>                Baud rate (default: 115200)\n"
              << "  --sync_count <n>             Number of TIME_SYNC rounds (default: 10)\n"
              << "  --interval_ms <ms>           Interval between rounds (default: 1000)\n"
              << "  --cmd_timeout_ms <ms>        Command response timeout (default: 1000)\n"
              << "  --do_time_set[=true|false]   Send TIME_SET once at startup (default: true)\n"
              << "  --drift_interval_ms <ms>       Interval for drift test rounds (default: 100)\n"
              << "  --run_drift_test               Run continuous drift test with short intervals\n"
              << "  --vid <hex>                  USB VID filter (default: 0x2E8A)\n"
              << "  --pid <hex>                  USB PID filter (default: 0x000A)\n"
              << "  --help                       Show this help\n";
}

bool parse_options(int argc, char **argv, DemoOptions *options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        }
        if (consume_string_arg(i, argc, argv, "--port", &options->port)) {
            continue;
        }
        if (consume_uint32_arg(i, argc, argv, "--baud", &options->baud)) {
            continue;
        }
        if (consume_int_arg(i, argc, argv, "--sync_count", &options->sync_count)) {
            continue;
        }
        if (consume_int_arg(i, argc, argv, "--interval_ms", &options->interval_ms)) {
            continue;
        }
        if (consume_int_arg(i, argc, argv, "--cmd_timeout_ms", &options->cmd_timeout_ms)) {
            continue;
        }
        if (consume_bool_arg(i, argc, argv, "--do_time_set", &options->do_time_set)) {
            continue;
        }
        if (consume_int_arg(i, argc, argv, "--drift_interval_ms", &options->drift_interval_ms)) {
            continue;
        }
        if (consume_bool_arg(i, argc, argv, "--run_drift_test", &options->run_drift_test)) {
            continue;
        }
        std::string value;
        if (consume_string_arg(i, argc, argv, "--vid", &value)) {
            if (!parse_hex_u16(value, &options->vid)) {
                std::cerr << "Invalid --vid: " << value << "\n";
                return false;
            }
            continue;
        }
        if (consume_string_arg(i, argc, argv, "--pid", &value)) {
            if (!parse_hex_u16(value, &options->pid)) {
                std::cerr << "Invalid --pid: " << value << "\n";
                return false;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        print_usage(argv[0]);
        return false;
    }

    if (options->sync_count <= 0 || options->interval_ms < 0 || options->cmd_timeout_ms <= 0) {
        std::cerr << "Invalid numeric options\n";
        return false;
    }
    return true;
}

void poll_serial(protocol::Parser &parser, serial::Serial &serial, int poll_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(poll_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t buf[256];
        const size_t n = serial.read(buf, sizeof(buf));
        if (n == 0) {
            continue;
        }
        parser.set_receive_time(now_steady_us());
        parser.feed(buf, n);
    }
}

bool wait_for_rsp(std::deque<TimedFrame> &rx_queue,
                  protocol::Parser &parser,
                  serial::Serial &serial,
                  uint8_t seq,
                  uint8_t expected_orig_msgid,
                  int timeout_ms,
                  TimedFrame *out) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        while (!rx_queue.empty()) {
            TimedFrame frame = std::move(rx_queue.front());
            rx_queue.pop_front();
            if (frame.frame.type != protocol::FrameType::kRsp) {
                continue;
            }
            if (frame.frame.seq != seq) {
                continue;
            }
            if (frame.frame.data.size() < 2) {
                continue;
            }
            if (frame.frame.data[0] != expected_orig_msgid) {
                continue;
            }
            if (frame.frame.data[1] != 0x00) {
                std::cerr << "RSP returned non-OK status: 0x"
                          << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(frame.frame.data[1]) << std::dec << "\n";
                return false;
            }
            *out = std::move(frame);
            return true;
        }

        poll_serial(parser, serial, 20);
    }
    return false;
}

bool send_cmd_and_wait_rsp(serial::Serial &serial,
                           protocol::Parser &parser,
                           std::deque<TimedFrame> &rx_queue,
                           uint8_t seq,
                           uint8_t msgid,
                           const std::vector<uint8_t> &payload,
                           int timeout_ms,
                           TimedFrame *out_rsp) {
    const std::vector<uint8_t> frame =
        protocol::build_frame(protocol::FrameType::kCmd, 0, seq, msgid, payload);
    serial.write(frame);
    return wait_for_rsp(rx_queue, parser, serial, seq, msgid, timeout_ms, out_rsp);
}

}  // namespace

int main(int argc, char **argv) {
    DemoOptions options;
    if (!parse_options(argc, argv, &options)) {
        return 1;
    }

    const std::optional<serial::PortInfo> port = find_port(options);
    if (!port.has_value()) {
        std::cerr << "No matching serial device found\n";
        return 1;
    }

    std::cout << "Using port: " << port->port << " @ " << options.baud << "\n";

    std::deque<TimedFrame> rx_queue;
    protocol::Parser parser([&rx_queue](const protocol::Frame &frame, uint64_t receive_time_us) {
        rx_queue.push_back(TimedFrame{frame, receive_time_us});
    });

    serial::Serial serial(
        port->port,
        options.baud,
        serial::Timeout::simpleTimeout(20),
        serial::bytesize_t::eightbits,
        serial::parity_t::parity_none,
        serial::stopbits_t::stopbits_one,
        serial::flowcontrol_t::flowcontrol_none,
        serial::dtrcontrol_t::dtr_enable,
        serial::rtscontrol_t::rts_enable);

    uint8_t seq = 0;

    if (options.do_time_set) {
        TimedFrame rsp;
        std::vector<uint8_t> payload;
        pack_u64_le(payload, now_unix_us());
        if (!send_cmd_and_wait_rsp(serial, parser, rx_queue, seq++, kMsgTimeSet, payload,
                                   options.cmd_timeout_ms, &rsp)) {
            std::cerr << "TIME_SET failed\n";
            return 1;
        }
        std::cout << "TIME_SET OK\n";
    }

    int ok_count = 0;
    int fail_count = 0;
    long double offset_abs_sum = 0.0L;
    long double delay_sum = 0.0L;

    // Run drift test if requested
    if (options.run_drift_test) {
        std::cout << "\n=== Running continuous drift test with " << options.drift_interval_ms
                  << "ms interval ===\n";
        // Perform initial sync rounds
        do_sync_rounds(10, options.drift_interval_ms);

        // Continuous monitoring
        int round = 11;
        while (true) {
            int64_t last_offset = do_sync_rounds(1, options.drift_interval_ms);
            std::cout << "[Continuous #" << round << "] offset=" << last_offset << " us\n";
            ++round;
        }
    }

    struct DriftPoint {
        int wait_minutes = 0;
        int64_t offset_us = 0;
        bool valid = false;
    };
    std::vector<DriftPoint> drift_results;

    // Run drift test if requested
    if (options.run_drift_test) {
        std::cout << "\n=== Running continuous drift test with " << options.drift_interval_ms
                  << "ms interval ===\n";
        // Perform initial sync rounds
        do_sync_rounds(10, options.drift_interval_ms);

        // Continuous monitoring
        int round = 11;
        while (true) {
            int64_t last_offset = do_sync_rounds(1, options.drift_interval_ms);
            std::cout << "[Continuous #" << round << "] offset=" << last_offset << " us\n";
            ++round;
        }
    }

    // Helper: perform N rounds of sync and return average offset
    auto do_sync_rounds = [&](int rounds, int interval_ms) -> int64_t {
        int64_t last_offset = 0;
        int64_t last_offset = 0;
        for (int i = 0; i < rounds; ++i) {
            const uint64_t t1 = now_steady_us();
            std::vector<uint8_t> sync_payload;
            pack_u64_le(sync_payload, t1);

            TimedFrame sync_rsp;
            if (!send_cmd_and_wait_rsp(serial, parser, rx_queue, seq++, kMsgTimeSync, sync_payload,
                                       options.cmd_timeout_ms, &sync_rsp)) {
                ++fail_count;
                std::cout << "[" << (i + 1) << "/" << rounds << "] TIME_SYNC timeout/fail\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
                continue;
            }

            if (sync_rsp.frame.data.size() < 26) {
                ++fail_count;
                std::cout << "[" << (i + 1) << "/" << rounds << "] TIME_SYNC response too short\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
                continue;
            }

            const uint64_t T1 = read_u64_le(sync_rsp.frame.data, 2);
            const uint64_t T2 = read_u64_le(sync_rsp.frame.data, 10);
            const uint64_t T3 = read_u64_le(sync_rsp.frame.data, 18);
            const uint64_t T4 = sync_rsp.receive_time_us;

            const int64_t delay = signed_diff_u64(T4, T1) - signed_diff_u64(T3, T2);
            const int64_t offset = (signed_diff_u64(T2, T1) + signed_diff_u64(T3, T4)) / 2;

            std::vector<uint8_t> adjust_payload;
            pack_s64_le(adjust_payload, -offset);
            TimedFrame adjust_rsp;
            if (!send_cmd_and_wait_rsp(serial, parser, rx_queue, seq++, kMsgTimeAdjust, adjust_payload,
                                       options.cmd_timeout_ms, &adjust_rsp)) {
                ++fail_count;
                std::cout << "[" << (i + 1) << "/" << rounds << "] TIME_ADJUST fail"
                          << " delay=" << delay << "us"
                          << " offset=" << offset << "us\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
                continue;
            }

            ++ok_count;
            offset_abs_sum += static_cast<long double>(std::llabs(offset));
            delay_sum += static_cast<long double>(delay);
            last_offset = offset;

            std::cout << "[" << (i + 1) << "/" << rounds << "]"
                      << " T1=" << T1
                      << " T2=" << T2
                      << " T3=" << T3
                      << " T4=" << T4
                      << " delay=" << delay << "us"
                      << " offset=" << offset << "us"
                      << " adjust=" << (-offset) << "us\n";

            if (i + 1 < rounds) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        }
        return last_offset;
    };

    // Initial 10 rounds
    std::cout << "\n=== Phase 1: Initial 10 rounds fast sync ===\n";
    do_sync_rounds(10, options.interval_ms);

    // Wait 2 minutes, then sync once
    std::cout << "\n=== Phase 2: Wait 2 minutes, then sync ===\n";
    std::this_thread::sleep_for(std::chrono::minutes(2));
    int64_t offset_2min = do_sync_rounds(1, 0);
    drift_results.push_back({2, offset_2min, offset_2min != 0});

    // Wait 5 minutes, then 10 rounds
    std::cout << "\n=== Phase 3: Wait 5 minutes, then 10 rounds ===\n";
    std::this_thread::sleep_for(std::chrono::minutes(5));
    do_sync_rounds(10, options.interval_ms);

    // Wait 10 minutes, then 10 rounds
    std::cout << "\n=== Phase 4: Wait 10 minutes, then 10 rounds ===\n";
    std::this_thread::sleep_for(std::chrono::minutes(10));
    int64_t offset_10min = do_sync_rounds(1, 0);
    drift_results.push_back({10, offset_10min, offset_10min != 0});

    std::cout << "\nSummary:\n";
    std::cout << "  total=" << options.sync_count << "\n";
    std::cout << "  ok=" << ok_count << "\n";
    std::cout << "  fail=" << fail_count << "\n";
    if (ok_count > 0) {
        std::cout << "  avg_abs_offset_us=" << static_cast<double>(offset_abs_sum / ok_count) << "\n";
        std::cout << "  avg_delay_us=" << static_cast<double>(delay_sum / ok_count) << "\n";
    }

    std::cout << "\nDrift Results:\n";
    for (const auto &drift : drift_results) {
        if (drift.valid) {
            std::cout << "  " << drift.wait_minutes << " min: offset=" << drift.offset_us << " us\n";
        }
    }

    return fail_count == 0 ? 0 : 2;
}

