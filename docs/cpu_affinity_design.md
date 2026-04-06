# CPU Affinity and Thread Orchestration Design

## Problem Statement

When running both `pc_client` (Pico INA228 power monitor) and `onboard_power_sampler` (Jetson Nano hwmon power monitor) simultaneously on Jetson Nano, we need to minimize jitter and ensure precise timing for:

1. **Pico data reception** - 1kHz USB serial streaming
2. **Jetson hwmon sampling** - 1kHz sysfs polling
3. **Cross-correlation accuracy** - timestamp alignment between two power sources

## Current Thread Architecture

### PC Client (`host_pc_client`)
- **Read Thread** - USB serial polling (`read_thread.cpp`)
- **Processor Thread** - Sample processing loop
- **Main Thread** - CLI and coordination
- (No Refresh Thread exists in the current implementation.)

### Onboard Power Sampler (`onboard_power_sampler`)
- **Main Thread** - Single-threaded hwmon polling with `clock_nanosleep()`
- Already supports `--cpu-core` and `--rt-prio` parameters

### System Threads (Uncontrollable)
- **USB IRQ handlers** - Kernel-level, cannot bind to core
- **SoftIRQ/Interrupt threads** - May introduce jitter

## Jetson Nano Hardware

- **4x Cortex-A57 cores** (CPU0-CPU3)
- **Thermal throttling** - Can affect timing at high load
- **DVFS** - Frequency scaling 102MHz - 2.04GHz
- **GPU** - Independent power domain

## Proposed Affinity Schemes

### Scheme A: Isolated Core Assignment (Recommended for Real-time)

```
CPU0: System + IRQ handling (default)
CPU1: onboard_power_sampler (isolated)
CPU2: pc_client read_thread (isolated)
CPU3: pc_client processor + refresh + main
```

**Pros:**
- Isolates real-time threads from system noise
- Clear separation of responsibilities
- Predictable cache behavior

**Cons:**
- CPU1/CPU2 may be underutilized
- Requires kernel parameter `isolcpus=1,2` for full isolation

**Implementation:**
```bash
# Onboard sampler on CPU1 with RT priority 80
sudo ./onboard_power_sampler --cpu-core 1 --rt-prio 80 --period-us 1000

# PC client threads need code modification to set affinity
# (see implementation section below)
```

### Scheme B: Shared Core with RT Priority

```
CPU0: System + IRQ
CPU1: BOTH onboard_sampler + pc_client read_thread (RT priority 80-90)
CPU2: pc_client processor + refresh
CPU3: Reserved for other workloads
```

**Pros:**
- Better core utilization
- Easier to implement

**Cons:**
- Cache contention between two samplers
- More jitter expected
- RT priority alone may not prevent interference

### Scheme C: Priority-Based Scheduling (No Affinity)

```
All CPUs: Default scheduler
RT Priority 90: onboard_power_sampler
RT Priority 85: pc_client read_thread
RT Priority 50: pc_client processor
RT Priority 10: pc_client refresh
```

**Pros:**
- Simplest implementation
- No core isolation needed

**Cons:**
- Still affected by IRQ handling
- Cache thrashing between cores
- Thermal throttling affects all threads

## Implementation Plan

### Phase 1: Add Affinity Support to PC Client

Add thread affinity APIs to `pc_client`:

```cpp
// In read_thread.h
void set_cpu_affinity(int core_id);
void set_realtime_priority(int prio);

// In power_monitor_session.h
void set_read_thread_affinity(int core_id);
void set_processor_thread_affinity(int core_id);
```

### Phase 2: Benchmark Harness

Create `cpu_affinity_benchmark.cpp`:

```cpp
// Measures jitter under different affinity configurations
// 1. Baseline (no affinity)
// 2. RT priority only
// 3. Affinity only
// 4. Affinity + RT priority
// 5. Isolated cores (if kernel supports)
```

Metrics:
- Timestamp jitter (std dev from expected period)
- Max latency spike
- Cache miss rate (via `perf`)
- CPU temperature and throttling events

### Phase 3: Experimental Matrix

| Config | Sampler Core | Read Core | RT Prio | Isolation | Expected Jitter |
|--------|-------------|-----------|---------|-----------|-----------------|
| Baseline | - | - | - | No | High |
| RT-only | - | - | 80/85 | No | Medium |
| Affinity | 1 | 2 | - | No | Medium |
| RT+Affinity | 1 | 2 | 80/85 | No | Low |
| Isolated | 1 | 2 | 80/85 | Yes | Very Low |

Each test runs for 60 seconds at 1kHz.

### Phase 4: Kernel-Level Isolation (Optional)

If hardware jitter is still problematic:

```bash
# Edit /boot/extlinux/extlinux.conf
APPEND isolcpus=1,2 nohz_full=1,2 rcu_nocbs=1,2

# This prevents scheduler from using CPU1/CPU2 for normal tasks
# Only threads explicitly pinned to these cores will run there
```

## Expected Results

### Jitter Sources Analysis

1. **USB IRQ latency**: ~10-50µs (unavoidable)
2. **Scheduler latency** (non-RT): ~100-500µs worst case
3. **Scheduler latency** (RT SCHED_FIFO): ~10-50µs
4. **Cache miss penalty**: ~100-300 cycles (~50-150ns per miss)
5. **Thermal throttling**: Can add ms-level delays

### Target Performance

- **Timestamp jitter** < 100µs (std dev)
- **Max spike** < 1ms
- **Sample loss** < 0.01%

## Open Questions

1. **USB IRQ affinity**: Can we bind USB interrupt handler to CPU0?
   - Check `/proc/interrupts` for IRQ numbers
   - Use `irqbalance` or manual `/proc/irq/X/smp_affinity`

2. **Memory locking**: Should we use `mlockall()` to prevent page faults?
   - Tradeoff: Resident set size vs latency

3. **CPU Frequency Locking**: Should we disable DVFS during critical tests?
   - `cpufreq-set -g performance` on all cores

4. **GPU Impact**: If GPU is active (e.g., running XR workload), does it affect CPU timing?
   - Need separate test with GPU load

## Experimental Protocol

### Test Script: `run_affinity_benchmark.sh`

```bash
#!/bin/bash
# 1. Set CPU governor to performance
# 2. Run baseline test (60s)
# 3. Run RT-only test
# 4. Run affinity test
# 5. Run RT+affinity test
# 6. Collect results and generate plots
```

### Analysis Notebook: `analyze_affinity.ipynb`

- Parse timestamps from both power monitors
- Calculate jitter statistics
- Plot CDF of latency
- Compare configurations

## Implementation Files to Create

1. `powermonitor/pc_client/src/thread_affinity.cpp` - Affinity API implementation
2. `powermonitor/pc_client/include/thread_affinity.h` - Header
3. `powermonitor/benchmarks/cpu_affinity_benchmark.cpp` - Benchmark harness
4. `powermonitor/scripts/run_affinity_benchmark.sh` - Test runner
5. `powermonitor/notebooks/analyze_affinity.ipynb` - Analysis notebook

## Risk Mitigation

1. **Thermal Throttling**: Monitor temperature with `tegrastats`
2. **Permission Denied**: RT priority and affinity may require `sudo` or `ulimit -r unlimited`
3. **System Instability**: RT threads can starve kernel - always have a kill switch
4. **Measurement Overhead**: `clock_nanosleep()` and timestamp calls add ~1-5µs overhead

## Success Criteria

- [ ] Affinity API implemented in pc_client
- [ ] Benchmark runs successfully on Nano
- [ ] Jitter reduced by >50% compared to baseline
- [ ] Documentation updated with recommended configuration
- [ ] Integration with existing power monitor workflow

## Next Steps

1. Implement thread affinity API in `pc_client`
2. Create benchmark harness
3. Run experiments on Jetson Nano
4. Analyze results and determine best configuration
5. Document findings in this file

## References

- `sched_setaffinity(2)` - CPU affinity
- `sched_setscheduler(2)` - RT scheduling
- `pthread_setaffinity_np(3)` - Thread-level affinity
- `isolcpus` kernel parameter - CPU isolation
- `irqbalance` - IRQ affinity management
