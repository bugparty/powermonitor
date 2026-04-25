# CPU Affinity Quick Start Guide

## Overview

This guide explains how to use CPU affinity to reduce jitter when running `pc_client` and `onboard_power_sampler` simultaneously on Jetson Nano.

## Quick Test

### 1. Build the benchmark tool

```bash
cd ~/powermonitor/pc_client
mkdir -p build_linux
cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) cpu_affinity_benchmark
```

### 2. Run the benchmark suite (requires sudo)

```bash
cd ~/powermonitor/scripts
sudo ./run_affinity_benchmark.sh
```

This will test 6 configurations:
1. **baseline** - No affinity, no RT priority
2. **rt_only** - RT priority 80, no affinity
3. **affinity_core1** - Pinned to CPU1, no RT
4. **affinity_core2** - Pinned to CPU2, no RT
5. **rt_affinity_core1** - Pinned to CPU1 + RT priority 80
6. **rt_affinity_core2** - Pinned to CPU2 + RT priority 80

Results will be saved to `benchmark_results/affinity_<timestamp>/`.

### 3. Analyze results

```bash
cd benchmark_results/affinity_<timestamp>
cat all_results.csv
```

Example output:
```csv
config,core,prio,period_us,duration_s,samples,mean_ns,stddev_ns,max_abs_ns,p99_ns,p999_ns
baseline,-1,-1,1000,60,60000,5234.56,89123.45,823456.78,156234.00,423567.00
rt_only,-1,80,1000,60,60000,1234.56,12345.67,234567.89,45678.00,123456.00
affinity_core1,1,-1,1000,60,60000,2345.67,23456.78,345678.90,67890.00,156789.00
rt_affinity_core1,1,80,1000,60,60000,856.23,5678.90,89012.34,12345.00,34567.00
```

**Key metrics:**
- `stddev_ns` - Lower is better (less jitter)
- `p99_ns` - 99th percentile latency spike
- `p999_ns` - 99.9th percentile (worst-case outliers)

## Running Production Tests

### Option A: Scheme A (Isolated Cores) - **Recommended**

```bash
# Terminal 1: onboard_power_sampler on CPU1 with RT priority 80
sudo ./onboard_power_sampler \
    --cpu-core 1 \
    --rt-prio 80 \
    --period-us 1000 \
    --output onboard_power.csv

# Terminal 2: pc_client on CPU2 with RT priority 85
sudo ./host_pc_client \
    --config config.yaml \
    --cpu-core 2 \
    --rt-prio 85 \
    --output pico_power.csv
```

**Note:** `pc_client` currently needs code modification to support `--cpu-core` and `--rt-prio` flags. See implementation section below.

### Option B: RT Priority Only (No Affinity)

```bash
# Terminal 1: onboard_power_sampler with RT priority
sudo ./onboard_power_sampler \
    --rt-prio 80 \
    --period-us 1000 \
    --output onboard_power.csv

# Terminal 2: pc_client with RT priority
sudo ./host_pc_client \
    --config config.yaml \
    --rt-prio 85 \
    --output pico_power.csv
```

### Option C: Baseline (No Optimization)

```bash
# Terminal 1
./onboard_power_sampler --period-us 1000 --output onboard_power.csv

# Terminal 2
./host_pc_client --config config.yaml --output pico_power.csv
```

## Adding Affinity Support to PC Client

### Step 1: Modify `main.cpp`

Add command-line arguments for affinity:

```cpp
#include "thread_affinity.h"

// In main():
int cpu_core = -1;
int rt_prio = -1;

// Parse new CLI args:
app.add_option("--cpu-core", cpu_core, "Pin threads to CPU core");
app.add_option("--rt-prio", rt_prio, "Set RT priority (1-99)");

// After parsing:
if (cpu_core >= 0 || rt_prio > 0) {
    powermonitor::client::ThreadAffinity::SetRealtimeWithAffinity(cpu_core, rt_prio);
}
```

### Step 2: Apply to worker threads

In `power_monitor_session.cpp`, set affinity for each thread:

```cpp
#include "thread_affinity.h"

void PowerMonitorSession::start(...) {
    // In read thread:
    std::thread reader([this, cpu_core, rt_prio] {
        if (cpu_core >= 0 && rt_prio > 0) {
            ThreadAffinity::SetRealtimeWithAffinity(cpu_core, rt_prio);
        }
        // ... existing read thread code ...
    });

    // In processor thread:
    std::thread processor([this, cpu_core, rt_prio] {
        if (cpu_core >= 0) {
            ThreadAffinity::SetCpuAffinity(cpu_core + 1);  // Different core
        }
        // ... existing processor code ...
    });
}
```

## Kernel-Level CPU Isolation (Advanced)

For the ultimate low-jitter setup, isolate CPU1 and CPU2 from the scheduler:

### Step 1: Edit boot configuration

```bash
sudo nano /boot/extlinux/extlinux.conf
```

Find the `APPEND` line and add:

```
APPEND isolcpus=1,2 nohz_full=1,2 rcu_nocbs=1,2
```

### Step 2: Reboot

```bash
sudo reboot
```

### Step 3: Verify isolation

```bash
cat /sys/devices/system/cpu/isolated
# Should output: 1,2

cat /proc/cmdline | grep isolcpus
```

Now CPU1 and CPU2 are reserved exclusively for your threads.

## Monitoring and Debugging

### Check thread affinity

```bash
# Find your process PID
ps aux | grep onboard_power_sampler

# Check affinity
taskset -p <PID>

# Check priority
chrt -p <PID>
```

### Monitor CPU usage

```bash
# Install htop
sudo apt install htop

# Run with affinity highlighted
htop -t
```

### Monitor temperature

```bash
# Jetson Nano
tegrastats

# Or generic
watch -n 1 'cat /sys/class/thermal/thermal_zone*/temp'
```

## Troubleshooting

### "Operation not permitted" when setting RT priority

**Solution:**
```bash
# Allow user to set RT priority
sudo ulimit -r unlimited

# Or run with sudo
sudo ./onboard_power_sampler --rt-prio 80 ...
```

### High jitter even with RT priority

**Possible causes:**
1. **USB IRQ latency** - USB interrupts can preempt RT threads. Check `/proc/interrupts`.
2. **Thermal throttling** - Monitor temperature with `tegrastats`.
3. **CPU frequency scaling** - Set governor to `performance`:
   ```bash
   sudo cpupower frequency-set -g performance
   ```
4. **Cache contention** - Try different core assignments.

### "sched_setscheduler failed: Invalid argument"

**Solution:** Priority must be 1-99 for SCHED_FIFO. Check with:
```bash
chrt -m  # Show min/max priorities
```

## Performance Expectations

Based on similar systems:

| Configuration | Expected Jitter (stddev) | Max Spike |
|--------------|-------------------------|-----------|
| Baseline | 50-200 µs | 1-5 ms |
| RT Priority | 10-50 µs | 200-500 µs |
| Affinity | 10-50 µs | 200-500 µs |
| RT + Affinity | 5-20 µs | 50-200 µs |
| RT + Affinity + Isolated Cores | 2-10 µs | 20-100 µs |

**Note:** Actual results depend on workload, temperature, and hardware variation.

## Next Steps

1. Run the benchmark suite to find optimal configuration
2. Integrate affinity settings into your production scripts
3. Consider kernel-level isolation if jitter is still too high
4. Monitor temperature and thermal throttling during tests
5. Test under realistic load (e.g., with XR application running)

## References

- `cpu_affinity_design.md` - Full design documentation
- `run_affinity_benchmark.sh` - Automated test script
- `thread_affinity.h` - API reference
