# CPU Affinity & Real-time 调度测试指南

本指南旨在通过核心绑定（Core Pinning）和实时优先级（Real-time Priority）消除 Jetson Nano 上的调度抖动（Jitter），确保 `pc_client` 的时间同步精度和 `onboard_power_sampler` 的采样稳定性。

## 1. 核心架构：非对称任务分区 (Core Partitioning)

在 Jetson Nano (4 Core A57) 上，建议采取以下“硬编排”策略：

| 核心 (Core) | 建议任务分配 | 调度策略 | 优先级 | 理由 |
| :--- | :--- | :--- | :--- | :--- |
| **Core 0** | OS Background, `pc_client` UI/Main | `SCHED_OTHER` | 0 | 默认核心，承载不确定性最高的系统任务。 |
| **Core 1** | **`pc_client::ReadThread`** | **`SCHED_FIFO`** | **80** | **关键路径**。负责串口中断响应和 T2/T3 时间戳，必须隔离。 |
| **Core 2** | **`onboard_power_sampler`** | **`SCHED_FIFO`** | **50** | **关键路径**。保证 I2C/sysfs 采样周期的绝对稳定（1ms 采样）。 |
| **Core 3** | `pc_client::Processor` | `SCHED_RR` | 20 | 计算密集型。将其与读取线程隔离，防止 IO 阻塞读取。 |

---

## 2. 快速开始：运行 Benchmark

### 2.1 编译 benchmark 工具

```bash
cd ~/powermonitor/pc_client/build_linux
make cpu_affinity_benchmark
```

### 2.2 运行完整测试套件

```bash
cd ~/powermonitor/scripts
sudo ./run_affinity_benchmark.sh
```

这将自动测试多种配置，并在 `benchmark_results/` 下生成包含 P99/P99.9 尾部延迟的报告。

---

## 3. 实际部署示例 (生产环境)

### 方案 A: 极致性能（推荐，核心分区 + 实时优先级）

```bash
# Terminal 1: Onboard sampler 绑定核心 2
sudo ./onboard_power_sampler \
    --cpu-core 2 \
    --rt-prio 50 \
    --period-us 1000 \
    --output onboard.csv

# Terminal 2: PC client 编排核心 1 (Read) 和核心 3 (Proc)
sudo ./pc_client \
    --read-thread-core 1 \
    --proc-thread-core 3 \
    --rt-prio 80 \
    -o output.json
```

### 方案 B: 进阶优化（增加 IRQ 亲和性）

为了减少跨核延迟，建议将 UART 中断绑定到与 `ReadThread` 相同的核心：

```bash
# 找到串口（例如 ttyUSB0）的中断号
grep ttyUSB /proc/interrupts

# 将中断绑定到 Core 1 (掩码为 2, 即二进制 0010)
echo 2 | sudo tee /proc/irq/<IRQ_NUM>/smp_affinity
```

---

## 4. 验证与监控

### 4.1 检查线程状态
```bash
# 查看所有线程的绑定核心和优先级
ps -eLo pid,tid,class,rtprio,psr,comm | grep pc_client
```
*   `PSR`: 当前运行的核心 ID
*   `CLS`: 调度类 (FF=FIFO, RR=Round Robin)
*   `RTPRIO`: 实时优先级

### 4.2 预期性能指标 (Tail Latency)

| 配置 | Jitter (Avg) | P99 Jitter | Max Spike |
| :--- | :--- | :--- | :--- |
| **Baseline** | 50-150 µs | 300-500 µs | 2000-5000 µs |
| **RT Priority Only** | 10-30 µs | 80-150 µs | 500-1000 µs |
| **RT + Affinity** | **5-15 µs** | **20-50 µs** | **100-200 µs** |
| **Isolated Cores** | < 5 µs | < 20 µs | < 50 µs |

## 5. 常见问题 (FAQ)

1.  **权限不足**: `SCHED_FIFO` 需要 `sudo` 权限。
2.  **核心隔离失败**: 检查内核启动参数是否包含 `isolcpus=1,2`。
3.  **队列阻塞**: 如果 `ReadThread` 出现大 Spike，请检查 `SampleQueue` 是否存在互斥锁竞争。

---
## 参考文档

- `cpu_affinity_design.md` - 完整设计文档
- `thread_affinity.h` - 核心 API 定义
