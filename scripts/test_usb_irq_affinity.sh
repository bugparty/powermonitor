#!/bin/bash
# Simplified IRQ Affinity Test - USB Interrupt Only

set -e

RESULTS_DIR="$HOME/powermonitor/scripts/benchmark_results/irq_usb_test_$(date +%Y%m%d-%H%M%S)"
BENCHMARK_BIN="/home/bowman/powermonitor/pc_client/build_linux/bin/cpu_affinity_benchmark"

mkdir -p "$RESULTS_DIR"

echo "=== USB IRQ Affinity Test ==="
echo "Results: $RESULTS_DIR"
echo ""

# Test 1: Baseline (USB IRQ can use all cores)
echo "=== Test 1: Baseline ==="
echo "Resetting USB IRQ (124) to all cores..."
echo 3f | sudo tee /proc/irq/124/smp_affinity
cat /proc/irq/124/smp_affinity
echo ""

echo "Running baseline test (60s)..."
"$BENCHMARK_BIN" \
    --config baseline_usb_default \
    --core 1 \
    --prio 80 \
    --period 1000 \
    --duration 60 \
    --output "$RESULTS_DIR/baseline_usb_default_jitter.csv"

echo "Baseline complete."
echo ""
sleep 5

# Test 2: USB IRQ on Core 0 only
echo "=== Test 2: USB IRQ on Core 0 ==="
echo "Setting USB IRQ (124) to Core 0 only..."
echo 01 | sudo tee /proc/irq/124/smp_affinity
cat /proc/irq/124/smp_affinity
echo ""

echo "Running optimized test (60s)..."
"$BENCHMARK_BIN" \
    --config usb_irq_core0 \
    --core 1 \
    --prio 80 \
    --period 1000 \
    --duration 60 \
    --output "$RESULTS_DIR/usb_irq_core0_jitter.csv"

echo "Optimized test complete."
echo ""

# Reset
echo "Resetting USB IRQ affinity..."
echo 3f | sudo tee /proc/irq/124/smp_affinity > /dev/null

echo ""
echo "=== Results Summary ==="
for csv in "$RESULTS_DIR"/*_jitter.csv; do
    if [ -f "$csv" ]; then
        config=$(basename "$csv" _jitter.csv)
        echo "Config: $config"
        tail -1 "$csv" | awk -F',' '{printf "  Mean: %.2f ns, P99: %.2f ns, Max: %.2f ns\n", $7, $10, $9}'
    fi
done

echo ""
echo "Results saved to: $RESULTS_DIR"
