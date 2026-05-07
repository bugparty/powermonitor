// CPU Affinity Benchmark Harness
// Measures timestamp jitter under different affinity configurations
//
// Usage: sudo ./cpu_affinity_benchmark --config <name> --duration <seconds>
//
// Build:
//   g++ -O2 -std=c++17 -pthread -o cpu_affinity_benchmark \
//       cpu_affinity_benchmark.cpp thread_affinity.cpp \
//       -lrt -lpthread

#include "thread_affinity.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

struct Sample {
    int64_t expected_ns;
    int64_t actual_ns;
    int64_t jitter_ns;  // actual - expected
};

int64_t NowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void SamplerThread(int core_id, int rt_prio, int period_us,
                   std::vector<Sample>* samples) {
    using powermonitor::client::ThreadAffinity;

    // Apply affinity and RT priority if specified
    if (core_id >= 0 && rt_prio > 0) {
        ThreadAffinity::SetRealtimeWithAffinity(core_id, rt_prio);
    } else if (core_id >= 0) {
        ThreadAffinity::SetCpuAffinity(core_id);
    } else if (rt_prio > 0) {
        ThreadAffinity::SetRealtimePriority(rt_prio);
    }

    ThreadAffinity::PrintThreadInfo("sampler");

    const int64_t period_ns = period_us * 1000LL;
    int64_t next_tick = NowNs();
    int64_t start_ns = next_tick;

    samples->clear();
    samples->reserve(100000);

    // Warmup: skip first 100 samples
    for (int i = 0; i < 100; ++i) {
        next_tick += period_ns;
        timespec ts{next_tick / 1000000000LL, next_tick % 1000000000LL};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    }

    // Start measurement
    while (!g_stop.load()) {
        int64_t actual = NowNs();
        int64_t expected = next_tick;
        int64_t jitter = actual - expected;

        samples->push_back({expected, actual, jitter});

        next_tick += period_ns;
        timespec ts{next_tick / 1000000000LL, next_tick % 1000000000LL};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    }
}

void ComputeStats(const std::vector<Sample>& samples,
                  double& mean, double& stddev, double& max_abs,
                  double& p99, double& p999) {
    if (samples.empty()) {
        mean = stddev = max_abs = p99 = p999 = 0.0;
        return;
    }

    // Compute mean
    double sum = 0.0;
    for (const auto& s : samples) {
        sum += s.jitter_ns;
    }
    mean = sum / samples.size();

    // Compute stddev
    double sum_sq = 0.0;
    for (const auto& s : samples) {
        double diff = s.jitter_ns - mean;
        sum_sq += diff * diff;
    }
    stddev = std::sqrt(sum_sq / samples.size());

    // Compute max absolute jitter
    max_abs = 0.0;
    for (const auto& s : samples) {
        double abs_jitter = std::abs(s.jitter_ns);
        if (abs_jitter > max_abs) {
            max_abs = abs_jitter;
        }
    }

    // Compute percentiles
    std::vector<int64_t> sorted_jitter;
    sorted_jitter.reserve(samples.size());
    for (const auto& s : samples) {
        sorted_jitter.push_back(std::abs(s.jitter_ns));
    }
    std::sort(sorted_jitter.begin(), sorted_jitter.end());

    p99 = sorted_jitter[static_cast<size_t>(sorted_jitter.size() * 0.99)];
    p999 = sorted_jitter[static_cast<size_t>(sorted_jitter.size() * 0.999)];
}

void WriteCSV(const std::string& filename,
              const std::vector<Sample>& samples) {
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open " << filename << "\n";
        return;
    }

    f << "expected_ns,actual_ns,jitter_ns\n";
    for (const auto& s : samples) {
        f << s.expected_ns << "," << s.actual_ns << "," << s.jitter_ns << "\n";
    }

    std::cout << "Wrote " << samples.size() << " samples to " << filename << "\n";
}

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --config <name>      Configuration name (for output file)\n"
              << "  --core <id>          CPU core to bind (-1 = no affinity)\n"
              << "  --prio <1..99>       RT priority (-1 = no RT)\n"
              << "  --period <us>        Sample period in microseconds (default 1000)\n"
              << "  --duration <sec>     Test duration in seconds (default 60)\n"
              << "  --output <file.csv>  Output CSV file\n"
              << "  --help               Show this help\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string config_name = "baseline";
    int core_id = -1;
    int rt_prio = -1;
    int period_us = 1000;
    int duration_s = 60;
    std::string output_file = "";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                exit(1);
            }
            return argv[++i];
        };

        if (a == "--config") {
            config_name = need("--config");
        } else if (a == "--core") {
            core_id = std::stoi(need("--core"));
        } else if (a == "--prio") {
            rt_prio = std::stoi(need("--prio"));
        } else if (a == "--period") {
            period_us = std::stoi(need("--period"));
        } else if (a == "--duration") {
            duration_s = std::stoi(need("--duration"));
        } else if (a == "--output") {
            output_file = need("--output");
        } else if (a == "--help" || a == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "=== CPU Affinity Benchmark ===\n"
              << "Config: " << config_name << "\n"
              << "Core: " << core_id << "\n"
              << "Priority: " << rt_prio << "\n"
              << "Period: " << period_us << " us\n"
              << "Duration: " << duration_s << " s\n"
              << "Cores available: " << powermonitor::client::ThreadAffinity::GetNumCores() << "\n";

    std::vector<Sample> samples;
    std::thread sampler(SamplerThread, core_id, rt_prio, period_us, &samples);

    // Run for specified duration
    std::this_thread::sleep_for(std::chrono::seconds(duration_s));
    g_stop.store(true);
    sampler.join();

    // Compute statistics
    double mean, stddev, max_abs, p99, p999;
    ComputeStats(samples, mean, stddev, max_abs, p99, p999);

    std::cout << "\n=== Results ===\n"
              << "Samples: " << samples.size() << "\n"
              << std::fixed << std::setprecision(2)
              << "Mean jitter: " << mean << " ns\n"
              << "Stddev: " << stddev << " ns\n"
              << "Max abs: " << max_abs << " ns\n"
              << "P99: " << p99 << " ns\n"
              << "P99.9: " << p999 << " ns\n";

    // Write CSV if specified
    if (output_file.empty()) {
        output_file = config_name + "_jitter.csv";
    }
    WriteCSV(output_file, samples);

    // Write summary
    std::string summary_file = config_name + "_summary.txt";
    std::ofstream sf(summary_file);
    sf << "config,core,prio,period_us,duration_s,samples,mean_ns,stddev_ns,max_abs_ns,p99_ns,p999_ns\n"
       << config_name << "," << core_id << "," << rt_prio << "," << period_us << ","
       << duration_s << "," << samples.size() << ","
       << std::fixed << std::setprecision(2)
       << mean << "," << stddev << "," << max_abs << "," << p99 << "," << p999 << "\n";
    std::cout << "Wrote summary to " << summary_file << "\n";

    return 0;
}
