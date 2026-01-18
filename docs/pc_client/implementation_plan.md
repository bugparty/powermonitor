# pc_client 双线程架构实现计划

## 1. 现状分析

**现有实现状态**：
- 单线程架构（仅在main.cpp中）
- 已实现协议解析器（protocol::Parser）
- 已实现帧构建器（protocol::frame_builder）
- 已实现20-bit解包工具（protocol::unpack）
- 已集成依赖库（CLI11, nlohmann/json, yaml-cpp, serial）

**需要改造的关键点**：
- 将串口读取逻辑迁移到独立线程
- 实现线程安全的数据队列
- 重构主线程为命令发送和数据处理
- 添加优雅停止机制

---

## 2. 双线程架构设计

### 2.1 总体结构

```
┌─────────────────────────────────────────────────────────────┐
│                    PC Client Process                        │
│                                                             │
│  ┌─────────────────┐                ┌──────────────────┐   │
│  │   Read Thread   │                │   Main Thread    │   │
│  │                 │                │                  │   │
│  │  - 串口读取      │─────队列─────▶│  - 设备交互       │   │
│  │  - 帧解析        │                │  - 命令重试       │   │
│  │  - 采样入队      │                │  - 数据存储       │   │
│  │  - 统计更新      │                │  - 用户交互       │   │
│  └─────────────────┘                └──────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 新增类设计

```cpp
// Thread-safe sample queue (for high-throughput data)
class SampleQueue {
public:
    struct Sample {
        uint32_t seq;
        uint64_t host_timestamp_us;
        std::vector<uint8_t> raw_data;
        uint32_t device_timestamp_us;
    };
    // ... 接口同上
    void push(Sample&& sample);
    // ...
};

// Thread-safe response queue (for control frames like RSP, CFG_REPORT)
class ResponseQueue {
public:
    void push(protocol::Frame&& frame);
    bool pop_wait(protocol::Frame& frame, int timeout_ms);

private:
    std::queue<protocol::Frame> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Read Thread implementation
class ReadThread {
public:
    ReadThread(serial::Serial* serial, SampleQueue* sample_q, 
               ResponseQueue* response_q, std::atomic<bool>* stop_flag);
    ~ReadThread();
    void start();  // 必须在发送任何命令前启动
    // ...

private:
    void run();
    void on_frame(const protocol::Frame& frame);

    serial::Serial* serial_;
    SampleQueue* sample_q_;
    ResponseQueue* response_q_;
    // ...
};

// Session data accumulation
class Session {
public:
    struct Config {
        uint32_t current_lsb_nA;
        uint8_t adcrange;
        uint16_t stream_period_us;
        uint16_t stream_mask;
    };

    void add_sample(const SampleQueue::Sample& sample);
    void save(const std::string& filepath) const;
    void set_config(const Config& cfg) { config_ = cfg; }

private:
    Config config_;
    std::vector<nlohmann::json> samples_;
    mutable std::mutex mutex_;
};
```

Note: the implementation uses `frame.data` as the payload field and helper functions like `unpack_u32()` instead of protocol helpers that do not exist in the current library.


---

## 3. 线程安全队列设计

### 3.1 数据结构设计

```cpp
class SampleQueue {
public:
    struct Sample {
        uint32_t seq;
        uint64_t host_timestamp_us;
        std::vector<uint8_t> raw_data;
        uint32_t device_timestamp_us;
    };
    
private:
    std::queue<Sample> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_requested_{false};
    const size_t max_size_ = 10000*512;  // 约1GB内存
};
```

### 3.2 操作接口

```cpp
// 生产者（Read Thread）调用
void push(Sample&& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_) {
        // 队列满，丢弃最旧的（防止内存无限增长）
        queue_.pop();
    }
    queue_.push(std::move(sample));
    cv_.notify_one();
}

// 消费者（Main Thread）调用 - 非阻塞
bool pop(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    sample = std::move(queue_.front());
    queue_.pop();
    return true;
}

// 消费者调用 - 阻塞等待
bool pop_wait(Sample& sample) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });
    if (queue_.empty()) return false;  // 被停止信号唤醒
    sample = std::move(queue_.front());
    queue_.pop();
    return true;
}
```

**性能指标**：
- 入队/出队操作：~50-100ns（无竞争情况下）
- 1kHz采样率下，队列可缓冲数小时数据（取决于max_size设置）

---

## 4. Read Thread实现细节

### 4.1 线程入口函数

```cpp
void ReadThread::run() {
    protocol::Parser parser([this](const protocol::Frame& frame) {
        this->on_frame(frame);
    });

    std::vector<uint8_t> buffer(4096);

    while (running_ && !(*stop_flag)) {
        try {
            const size_t bytes_read = serial_->read(buffer.data(), buffer.size());
            if (bytes_read == 0) {
                continue;
            }

            buffer.resize(bytes_read);
            parser.feed(buffer);
            buffer.resize(4096);
        } catch (const serial::PortNotOpenedException& e) {
            break;
        } catch (const serial::SerialException& e) {
            break;
        }
    }

    running_ = false;
}
```

### 4.2 帧处理逻辑

```cpp
void ReadThread::on_frame(const protocol::Frame& frame) {
    stats_->rx_counts[frame.msgid].fetch_add(1, std::memory_order_relaxed);

    if (frame.msgid == kMsgDataSample) {
        if (frame.data.size() < kDataSampleMinSize) {
            stats_->crc_fail.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        SampleQueue::Sample sample;
        sample.seq = frame.seq;
        sample.host_timestamp_us = now_steady_us();
        sample.device_timestamp_us = unpack_u32(frame.data.data());
        sample.raw_data = frame.data;

        sample_q_->push(std::move(sample));
    } else {
        response_q_->push(protocol::Frame(frame));
    }
}
```

**错误处理策略**：
- 串口读取超时：继续循环（正常情况）
- 串口断开：退出线程
- 解析错误：更新CRC失败统计，继续
- 队列满：自动丢弃最旧数据，保证不阻塞

---

## 5. Main Thread实现细节

### 5.1 主流程重构

```cpp
// 原main函数重构为类方法
class PowerMonitorSession {
public:
    int run();
    
private:
    bool connect_device();
    bool initialize_device();
    void process_samples();
    void handle_commands();
    void save_and_exit();
    
    serial::Serial serial_;
    SampleQueue sample_queue_;
    ResponseQueue response_queue_; // 新增
    ReadThread read_thread_;
    Session session_;
    std::atomic<bool> stop_requested_{false};
};
```

### 5.2 设备初始化流程

```cpp
bool PowerMonitorSession::initialize_device() {
    // 关键修正：在发送任何命令前，必须先启动ReadThread，否则无法接收RSP
    read_thread_.start();

    // 1. PING
    if (!send_command_with_retry(kMsgPing, {})) {
        return false;
    }
    
    // 2. GET_CFG (获取当前配置)
    std::vector<uint8_t> cfg_data;
    if (!send_command_with_retry(kMsgGetCfg, {}, &cfg_data)) {
        return false;
    }
    
    // 3. 解析CFG_REPORT（将从config_queue获取）
    if (!wait_for_config_report(&cfg_data, kCmdTimeoutUs)) {
        return false;
    }
    
    Session::Config cfg = parse_cfg_report(cfg_data);
    session_.set_config(cfg);
    
    // 4. 如果用户有自定义配置，发送SET_CFG
    if (user_config_provided_) {
        if (!send_command_with_retry(kMsgSetCfg, build_set_cfg_payload())) {
            return false;
        }
        // SET_CFG后会自动发送CFG_REPORT
        if (!wait_for_config_report(&cfg_data, kCmdTimeoutUs)) {
            return false;
        }
        cfg = parse_cfg_report(cfg_data);
        session_.set_config(cfg);
    }
    
    // 5. STREAM_START
    return send_command_with_retry(kMsgStreamStart, build_stream_start_payload());
}
```

### 5.3 命令重试逻辑

```cpp
bool PowerMonitorSession::send_command_with_retry(
    uint8_t msgid, const std::vector<uint8_t>& payload,
    std::vector<uint8_t>* rsp_data) {

    for (uint8_t retry = 0; retry <= kMaxRetries; ++retry) {
        try {
            std::vector<uint8_t> frame = protocol::build_frame(
                protocol::FrameType::kCmd, 0, cmd_seq_++, msgid, payload);
            const uint8_t sent_seq = cmd_seq_ - 1;

            serial_.write(frame);
            stats_.tx_counts[msgid].fetch_add(1, std::memory_order_relaxed);

            protocol::Frame rsp;
            if (wait_for_response_from_queue(sent_seq, &rsp, kCmdTimeoutUs)) {
                if (!rsp.data.empty() && rsp.data[0] == kStatusOk) {
                    if (rsp_data) {
                        *rsp_data = rsp.data;
                    }
                    return true;
                }
                return false;
            }

        } catch (const serial::SerialException& e) {
            log_error("Write failed: {}", e.what());
            return false;
        }

        stats_.timeouts.fetch_add(1, std::memory_order_relaxed);
        log_warn("Command 0x{:02X} timeout, retry {}/{}",
                 msgid, retry + 1, kMaxRetries);
    }

    return false;
}
```

---

## 6. 同步与关闭机制

### 6.1 优雅停止流程

```
用户请求停止 (Ctrl+C)
    ↓
设置 stop_requested = true
    ↓
发送 STREAM_STOP（带重试）
    ↓
等待 Read Thread 退出（join）
    ↓
处理队列中剩余的所有采样
    ↓
保存 JSON 文件
    ↓
关闭串口
    ↓
退出进程
```

**关键实现**：

```cpp
void PowerMonitorSession::stop() {
    stop_requested_ = true;
    
    // 尝试停止设备流
    send_command_with_retry(kMsgStreamStop, {});
    
    // 通知队列停止
    sample_queue_.stop();
    
    // 等待读线程退出
    read_thread_.join();
    
    // 清空队列
    process_remaining_samples();
    
    // 保存数据
    session_.save(output_file_);
}
```

---

## 7. 数据存储优化

### 7.1 预分配优化

```cpp
class Session {
    // 预分配vector容量，避免频繁realloc
    Session() {
        samples_.reserve(100000);  // 预分配约100k采样，20MB
    }
    
    void add_sample(const SampleQueue::Sample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        samples_.push_back(convert_to_json(sample));
    }
};
```

### 7.2 批量写入JSON

```cpp
void Session::save(const std::string& filepath) const {
    nlohmann::json root;
    root["meta"] = build_meta_json();
    root["samples"] = nlohmann::json::array();
    
    // 批量添加，减少JSON库开销
    for (const auto& sample : samples_) {
        root["samples"].push_back(sample);
    }
    
    // 使用更快的写入方式
    std::ofstream file(filepath);
    file << root.dump(-1) << std::endl;  // -1 = 最小化空格
}
```

---

## 8. 错误处理完整策略

### 8.1 设备断开检测

```cpp
// Read Thread中
void ReadThread::run() {
    while (running_) {
        try {
            bytes_read = serial_->read(...);
        } catch (serial::PortNotOpenedException&) {
            // 设备断开
            stop_flag_->store(true);
            break;
        }
    }
}

// Main Thread中
void PowerMonitorSession::run() {
    while (!stop_requested_) {
        if (!serial_.isOpen()) {
            log_error("Device disconnected!");
            break;
        }
        // ...
    }
    save_and_exit();
}
```

### 8.2 通信超时检测

```cpp
// 5秒无数据视为超时
void PowerMonitorSession::check_data_timeout() {
    uint64_t now = now_steady_us();
    if (now - last_data_ts_ > kDataTimeoutUs) {
        log_error("No data received for {} seconds", kDataTimeoutUs / 1e6);
        stop_requested_ = true;
    }
}
```

---

## 9. 性能优化建议

### 9.1 减少锁竞争

- **批量出队**：一次取出多个采样（如100个/批次）
  
  ```cpp
  std::vector<SampleQueue::Sample> batch;
  while (batch.size() < 100 && sample_queue_.pop(item)) {
      batch.push_back(std::move(item));
  }
  ```
- **双重检查锁**：在关键路径上

### 9.2 CPU亲和性

- 将Read Thread绑定到独立CPU核心（Linux）
- 减少上下文切换开销

### 9.3 数据零拷贝

- 尽量移动语义（std::move）而非拷贝
- 使用std::string_view处理子字符串

---

## 10. 测试覆盖需求

需要添加的测试用例：

1. **基本功能测试**
   - 单线程模式：验证现有功能正常
   - 双线程模式：验证数据不丢失

2. **边界条件测试**
   - 队列满载：验证丢弃策略
   - 采样速率突增：验证队列不崩溃

3. **错误场景测试**
   - 设备断开：验证数据保存
   - 通信超时：验证退出机制
   - 停止标志竞争：验证无死锁

---

## 11. 实施步骤

**优先级顺序**：

1. **P0 - 核心功能**
   - [ ] 实现SampleQueue（线程安全队列）
   - [ ] 实现ReadThread类
   - [ ] 重构main()为PowerMonitorSession
   - [ ] 实现设备初始化流程
   - [ ] 实现优雅停止

2. **P1 - 可靠性**
   - [ ] 命令重试逻辑
   - [ ] 错误处理（串口断开、超时）
   - [ ] 数据完整性验证

3. **P2 - 性能优化**
   - [ ] 批量处理优化
   - [ ] 内存使用监控

4. **P3 - 可观测性**
   - [ ] 实时统计输出
   - [ ] 进度条显示
   - [ ] 详细的debug日志

---

## 12. 技术风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 数据丢失 | 高 | 中 | 添加队列满日志，监控丢包率 |
| 内存溢出 | 高 | 中 | 限流控制，超过阈值自动停止 |
| 死锁 | 高 | 低 | 使用RAII锁管理器，代码审查 |
| 竞态条件 | 中 | 中 | ThreadSanitizer验证，添加注释 |
| 性能瓶颈 | 中 | 中 | 基准测试，性能分析（perf） |

---

## 总结

此双线程架构方案：

✅ **符合文档设计**：线程职责分离明确  
✅ **足够健壮**：错误处理完整，优雅停止机制健全  
✅ **性能达标**：1kHz采样率下无压力，锁竞争最小化  
✅ **内存安全**：队列有界，防止内存无限增长  
✅ **可维护性高**：模块化设计，职责清晰  

**等待您的修改确认后，开始实施代码改造。**
