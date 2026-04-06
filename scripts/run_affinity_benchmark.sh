#!/bin/bash
# CPU Affinity Benchmark Runner
# Tests different affinity configurations and collects results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
POWERMONITOR_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$POWERMONITOR_ROOT/benchmark_results/affinity_$(date +%Y%m%d-%H%M%S)"

DURATION=${DURATION:-60}  # seconds
PERIOD=${PERIOD:-1000}    # microseconds

mkdir -p "$OUTPUT_DIR"

echo "=== CPU Affinity Benchmark Suite ==="
echo "Output directory: $OUTPUT_DIR"
echo "Duration per test: ${DURATION}s"
echo "Sample period: ${PERIOD}us"

# Check if we have sudo for RT priority
if [ "$EUID" -ne 0 ]; then
    echo "WARNING: Not running as root - RT priority may fail"
    echo "         Run with sudo for full functionality"
fi

# Set CPU governor to performance to minimize DVFS jitter
echo "Setting CPU governor to performance..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    if [ -w "$cpu" ]; then
        echo performance > "$cpu"
    fi
done

# Build benchmark tool
echo "Building benchmark tool..."
cd "$POWERMONITOR_ROOT/pc_client"
if [ ! -f build_linux/bin/cpu_affinity_benchmark ]; then
    mkdir -p build_linux
    cd build_linux
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc) cpu_affinity_benchmark
fi

BENCHMARK_BIN="$POWERMONITOR_ROOT/pc_client/build_linux/bin/cpu_affinity_benchmark"
if [ ! -f "$BENCHMARK_BIN" ]; then
    echo "ERROR: Benchmark binary not found at $BENCHMARK_BIN"
    exit 1
fi

# Test configurations
declare -a CONFIGS=(
    "baseline:-1:-1"
    "rt_only:-1:80"
    "affinity_core1:1:-1"
    "affinity_core2:2:-1"
    "rt_affinity_core1:1:80"
    "rt_affinity_core2:2:80"
)

# Run tests
cd "$OUTPUT_DIR"

for config_str in "${CONFIGS[@]}"; do
    IFS=':' read -r name core prio <<< "$config_str"

    echo ""
    echo "=== Running test: $name ==="
    echo "  Core: $core, Priority: $prio"

    "$BENCHMARK_BIN" \
        --config "$name" \
        --core "$core" \
        --prio "$prio" \
        --period "$PERIOD" \
        --duration "$DURATION" \
        --output "${name}_jitter.csv"

    sleep 2  # Cool down between tests
done

# Collect summaries
echo ""
echo "=== Collecting results ==="

SUMMARY_FILE="$OUTPUT_DIR/all_results.csv"
echo "config,core,prio,period_us,duration_s,samples,mean_ns,stddev_ns,max_abs_ns,p99_ns,p999_ns" > "$SUMMARY_FILE"

for config_str in "${CONFIGS[@]}"; do
    IFS=':' read -r name _ _ <<< "$config_str"
    if [ -f "${name}_summary.txt" ]; then
        tail -n 1 "${name}_summary.txt" >> "$SUMMARY_FILE"
    fi
done

echo ""
echo "=== Summary ==="
cat "$SUMMARY_FILE"

echo ""
echo "=== Benchmark complete ==="
echo "Results saved to: $OUTPUT_DIR"
echo ""
echo "Next steps:"
echo "  1. Analyze CSV files with analyze_affinity.ipynb"
echo "  2. Compare jitter CDFs across configurations"
echo "  3. Identify best affinity scheme for your workload"

# Restore CPU governor (optional)
# for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
#     if [ -w "$cpu" ]; then
#         echo powersave > "$cpu"
#     fi
# done
