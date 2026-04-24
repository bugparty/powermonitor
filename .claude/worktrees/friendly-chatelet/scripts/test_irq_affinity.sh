#!/bin/bash
# IRQ Affinity Test Script for Jetson Nano
# Tests impact of interrupt isolation on latency

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/benchmark_results/irq_affinity_$(date +%Y%m%d-%H%M%S)"
BENCHMARK_BIN="/home/bowman/powermonitor/pc_client/build_linux/bin/cpu_affinity_benchmark"

mkdir -p "$RESULTS_DIR"

echo "=== IRQ Affinity Benchmark ==="
echo "Results directory: $RESULTS_DIR"
echo ""

# Function to show current IRQ affinity
show_irq_affinity() {
    echo "Current IRQ affinity:"
    echo "  Timer (IRQ 13): $(cat /proc/irq/13/smp_affinity 2>/dev/null || echo 'N/A')"
    echo "  USB (IRQ 124): $(cat /proc/irq/124/smp_affinity 2>/dev/null || echo 'N/A')"
    echo ""
}

# Function to run benchmark
run_benchmark() {
    local config_name=$1
    echo "=== Running: $config_name ==="

    "$BENCHMARK_BIN" \
        --config "$config_name" \
        --core 1 \
        --prio 80 \
        --period 1000 \
        --duration 60 \
        --output "$RESULTS_DIR/${config_name}_jitter.csv"

    echo ""
}

# Save interrupt counts
save_interrupts() {
    local filename=$1
    cat /proc/interrupts > "$RESULTS_DIR/${filename}"
}

echo "STEP 0: Check prerequisites"
echo "Benchmark binary: $BENCHMARK_BIN"
if [ ! -f "$BENCHMARK_BIN" ]; then
    echo "ERROR: Benchmark binary not found"
    exit 1
fi
echo "✓ Benchmark binary found"
echo ""

# Baseline test (no IRQ affinity changes)
echo "=== TEST 1: Baseline (Default IRQ Affinity) ==="
show_irq_affinity
save_interrupts "baseline_interrupts_before.txt"

run_benchmark "baseline_default_irq"

save_interrupts "baseline_interrupts_after.txt"

echo "Baseline complete. Sleeping 10s..."
sleep 10
echo ""

# Timer interrupt isolation
echo "=== TEST 2: Timer IRQ on Core 0 ==="
echo "Configuring timer interrupt (IRQ 13) to Core 0 only..."
echo 1 > /proc/irq/13/smp_affinity
show_irq_affinity
save_interrupts "timer_irq_core0_before.txt"

run_benchmark "timer_irq_core0"

save_interrupts "timer_irq_core0_after.txt"

# Reset timer affinity
echo "Resetting timer IRQ affinity..."
echo ff > /proc/irq/13/smp_affinity

echo "Timer test complete. Sleeping 10s..."
sleep 10
echo ""

# USB interrupt isolation
echo "=== TEST 3: USB IRQ on Core 0 ==="
echo "Configuring USB interrupt (IRQ 124) to Core 0 only..."
echo 1 > /proc/irq/124/smp_affinity
show_irq_affinity
save_interrupts "usb_irq_core0_before.txt"

run_benchmark "usb_irq_core0"

save_interrupts "usb_irq_core0_after.txt"

# Reset USB affinity
echo "Resetting USB IRQ affinity..."
echo ff > /proc/irq/124/smp_affinity

echo "USB test complete. Sleeping 10s..."
sleep 10
echo ""

# Combined optimization
echo "=== TEST 4: Timer + USB IRQ on Core 0 ==="
echo "Configuring Timer (13) and USB (124) to Core 0..."
echo 1 > /proc/irq/13/smp_affinity
echo 1 > /proc/irq/124/smp_affinity
show_irq_affinity
save_interrupts "combined_irq_core0_before.txt"

run_benchmark "combined_irq_core0"

save_interrupts "combined_irq_core0_after.txt"

# Reset all
echo "Resetting all IRQ affinity..."
echo ff > /proc/irq/13/smp_affinity
echo ff > /proc/irq/124/smp_affinity

echo ""
echo "=== All tests complete ==="
echo "Results saved to: $RESULTS_DIR"
echo ""

# Summary
echo "=== Summary ==="
for csv in "$RESULTS_DIR"/*_jitter.csv; do
    if [ -f "$csv" ]; then
        config=$(basename "$csv" _jitter.csv)
        echo "Config: $config"
        tail -1 "$csv" | awk -F',' '{printf "  Mean: %.2f ns, Max: %.2f ns\n", $7, $9}'
    fi
done

echo ""
echo "Next steps:"
echo "  1. Compare Max Spike across configurations"
echo "  2. Identify which IRQ isolation is most effective"
echo "  3. Combine with CPU affinity for optimal results"
