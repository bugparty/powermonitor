#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop.store(true);
}

struct Config {
    std::string hwmon_path = "/sys/class/hwmon/hwmon1";
    uint64_t period_us = 1000;
    double duration_s = 0.0;  // 0 = run until Ctrl-C
    std::string output = "-"; // "-" = stdout
    int rt_prio = -1;         // <0 disabled
    int cpu_core = -1;        // <0 disabled
    int sensor_samples = -1;  // <0 unchanged
    int update_interval = -1; // <0 unchanged
};

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return std::nullopt;
    }
    std::string v;
    std::getline(f, v);
    return v;
}

bool write_text_file(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f.is_open()) {
        return false;
    }
    f << value;
    return f.good();
}

std::optional<int64_t> read_int_file(const std::string& path) {
    auto txt = read_text_file(path);
    if (!txt.has_value()) {
        return std::nullopt;
    }
    try {
        size_t idx = 0;
        long long v = std::stoll(txt.value(), &idx, 10);
        return static_cast<int64_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}

bool apply_sched(const Config& cfg) {
    if (cfg.cpu_core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cfg.cpu_core, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) {
            std::cerr << "warn: sched_setaffinity failed: " << strerror(errno) << "\n";
        }
    }
    if (cfg.rt_prio >= 0) {
        struct sched_param sp {};
        sp.sched_priority = cfg.rt_prio;
        if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
            std::cerr << "warn: sched_setscheduler(SCHED_FIFO) failed: " << strerror(errno) << "\n";
            return false;
        }
    }
    return true;
}

bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "--hwmon") {
            const char* v = need("--hwmon");
            if (!v) return false;
            cfg.hwmon_path = v;
        } else if (a == "--period-us") {
            const char* v = need("--period-us");
            if (!v) return false;
            cfg.period_us = std::stoull(v);
        } else if (a == "--duration-s") {
            const char* v = need("--duration-s");
            if (!v) return false;
            cfg.duration_s = std::stod(v);
        } else if (a == "--output") {
            const char* v = need("--output");
            if (!v) return false;
            cfg.output = v;
        } else if (a == "--rt-prio") {
            const char* v = need("--rt-prio");
            if (!v) return false;
            cfg.rt_prio = std::stoi(v);
        } else if (a == "--cpu-core") {
            const char* v = need("--cpu-core");
            if (!v) return false;
            cfg.cpu_core = std::stoi(v);
        } else if (a == "--sensor-samples") {
            const char* v = need("--sensor-samples");
            if (!v) return false;
            cfg.sensor_samples = std::stoi(v);
        } else if (a == "--update-interval") {
            const char* v = need("--update-interval");
            if (!v) return false;
            cfg.update_interval = std::stoi(v);
        } else if (a == "--help" || a == "-h") {
            std::cout
                << "Usage: onboard_power_sampler [options]\n"
                << "  --hwmon <path>            default: /sys/class/hwmon/hwmon1\n"
                << "  --period-us <us>          sample period in microseconds (default 1000)\n"
                << "  --duration-s <sec>        run duration, 0=until Ctrl-C\n"
                << "  --output <path|->         CSV output path, '-' for stdout\n"
                << "  --rt-prio <1..99>         set SCHED_FIFO priority\n"
                << "  --cpu-core <id>           pin sampler process to one CPU core\n"
                << "  --sensor-samples <n>      write hwmon samples before start\n"
                << "  --update-interval <ms>    write hwmon update_interval before start\n";
            exit(0);
        } else {
            std::cerr << "unknown arg: " << a << "\n";
            return false;
        }
    }
    return true;
}

int64_t now_ns(clockid_t cid) {
    struct timespec ts {};
    clock_gettime(cid, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

timespec ns_to_ts(int64_t ns) {
    timespec ts {};
    ts.tv_sec = ns / 1000000000LL;
    ts.tv_nsec = ns % 1000000000LL;
    if (ts.tv_nsec < 0) {
        ts.tv_nsec += 1000000000LL;
        ts.tv_sec -= 1;
    }
    return ts;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        std::cerr << "use --help for usage\n";
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (cfg.update_interval >= 0) {
        if (!write_text_file(cfg.hwmon_path + "/update_interval", std::to_string(cfg.update_interval))) {
            std::cerr << "warn: failed to write update_interval (need sudo?)\n";
        }
    }
    if (cfg.sensor_samples >= 0) {
        if (!write_text_file(cfg.hwmon_path + "/samples", std::to_string(cfg.sensor_samples))) {
            std::cerr << "warn: failed to write samples (need sudo?)\n";
        }
    }

    apply_sched(cfg);

    std::ostream* out = &std::cout;
    std::ofstream out_file;
    if (cfg.output != "-") {
        out_file.open(cfg.output, std::ios::out | std::ios::trunc);
        if (!out_file.is_open()) {
            std::cerr << "failed to open output: " << cfg.output << "\n";
            return 3;
        }
        out = &out_file;
    }

    *out << "mono_ns,unix_ns,vdd_in_mw,vdd_cpu_gpu_cv_mw,vdd_soc_mw,total_mw\n";
    out->flush();

    const std::string in1 = cfg.hwmon_path + "/in1_input";
    const std::string i1 = cfg.hwmon_path + "/curr1_input";
    const std::string in2 = cfg.hwmon_path + "/in2_input";
    const std::string i2 = cfg.hwmon_path + "/curr2_input";
    const std::string in3 = cfg.hwmon_path + "/in3_input";
    const std::string i3 = cfg.hwmon_path + "/curr3_input";

    const int64_t period_ns = static_cast<int64_t>(cfg.period_us) * 1000LL;
    int64_t next_tick = now_ns(CLOCK_MONOTONIC);
    const int64_t start_ns = next_tick;
    const int64_t end_ns = (cfg.duration_s > 0.0) ? start_ns + static_cast<int64_t>(cfg.duration_s * 1e9) : 0;

    uint64_t count = 0;
    while (!g_stop.load()) {
        const auto v1 = read_int_file(in1);
        const auto c1 = read_int_file(i1);
        const auto v2 = read_int_file(in2);
        const auto c2 = read_int_file(i2);
        const auto v3 = read_int_file(in3);
        const auto c3 = read_int_file(i3);

        if (!v1 || !c1 || !v2 || !c2 || !v3 || !c3) {
            std::cerr << "read error from hwmon path: " << cfg.hwmon_path << "\n";
            return 4;
        }

        const int64_t p1 = (*v1) * (*c1) / 1000LL;
        const int64_t p2 = (*v2) * (*c2) / 1000LL;
        const int64_t p3 = (*v3) * (*c3) / 1000LL;
        const int64_t total = p1 + p2 + p3;

        const int64_t mono_ns = now_ns(CLOCK_MONOTONIC);
        const int64_t unix_ns = now_ns(CLOCK_REALTIME);
        *out << mono_ns << "," << unix_ns << "," << p1 << "," << p2 << "," << p3 << "," << total << "\n";

        ++count;
        if ((count & 0x3FF) == 0) {
            out->flush();
        }

        next_tick += period_ns;
        const timespec ts = ns_to_ts(next_tick);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);

        if (end_ns > 0 && now_ns(CLOCK_MONOTONIC) >= end_ns) {
            break;
        }
    }

    out->flush();
    return 0;
}
