# AGENTS.md

本文件用于指导 AI Agents 理解本项目结构和相关文档。

---

## 项目概述

本项目是一个基于 INA228 功率监测芯片的电源监测系统，包含：
- **device/**: 嵌入式设备端代码（Raspberry Pi Pico RP2040）
- **docs/**: 协议规格文档和技术文档
- **pc/**: PC 端协议仿真器（待实现）

---

## 文档索引

在开始任何开发工作前，**务必查阅以下文档**：

| 文档路径 | 内容描述 |
|---------|---------|
| `docs/INA228_uart_protocol.md` | **INA228 串口通信协议规格书** - 定义了 PC 与设备间的完整通信协议，包括帧格式、消息类型、数据格式、CRC 校验等 |
| `device/README.md` | 硬件信息、开发环境配置、编译和烧录指南 |
| `device/time_sync_documentation.md` | 时间同步模块的技术文档 |

---

## 关键协议要点（快速参考）

> 详细信息请查阅 `docs/INA228_uart_protocol.md`

- **帧格式**: SOF(0xAA 0x55) + Header + Payload + CRC16
- **CRC 算法**: CRC-16/CCITT-FALSE (Poly=0x1021, Init=0xFFFF)
- **字节序**: Little-Endian
- **20-bit 数据**: LE-packed 3 字节格式

---

# INA228 协议仿真器（PC 端）实现文档 v0.1

下面是一份可以直接丢给 agents 开工的**实现文档 / 任务规格**（面向"单进程事件循环 + 虚拟串口链路 + 设备侧仿真 + PC 侧协议栈"）。我按你最新协议（LEN 含 MSGID、CRC16-CCITT-FALSE、DATA/EVT 序号空间等）来写。


---


## 0. 目标


在 PC 上实现一个**纯软件仿真环境**，用于调试 INA228 协议的 PC/Device 通信，无需真实串口和硬件：


- 单进程、**事件循环（Event Loop）**驱动（不使用多线程也能跑）
- 提供 **Virtual Serial Link**（双向字节流），支持粘包/拆包/延迟/丢包/bit flip
- Device 侧仿真行为：


  - 收到 `SET_CFG`：回 `RSP(OK)`，紧接着发 `CFG_REPORT`
  - 收到 `STREAM_START(period_us, channel_mask)`：回 `RSP(OK)`，然后按 `period_us` 周期发送 `DATA_SAMPLE`
  - 电压/电流：用**简单波形**或**随机游走**生成 raw（20-bit packed）
- PC 侧实现协议栈：


  - 组包/拆包、CRC 校验、parser 状态机、命令超时重传（建议）
  - 解析 `CFG_REPORT` 并保存 `current_lsb_nA/adcrange`
  - 解析 `DATA_SAMPLE`，做丢帧检测（SEQ）并可选换算成工程值输出


---


## 1. 总体架构


### 1.1 模块划分


1. `protocol/`


- `crc16_ccitt_false.{h,cpp}`
- `frame_builder.{h,cpp}`：按协议封装帧（SOF、VER、TYPE、FLAGS、SEQ、LEN、MSGID、DATA、CRC）
- `parser.{h,cpp}`：字节流 parser（状态机），输出 Frame 对象（或回调）
- `unpack.{h,cpp}`：20-bit LE-packed 解包/符号扩展、工程值换算（可选）


1. `sim/`


- `virtual_link.{h,cpp}`：虚拟双向链路，故障注入（拆包/粘包/延迟/丢包/翻转）
- `event_loop.{h,cpp}`：事件循环 + 定时器队列（优先用单线程最小实现）


1. `node/`


- `pc_node.{h,cpp}`：PC 侧协议行为（发送命令、重传、处理 CFG/DATA）
- `device_node.{h,cpp}`：设备侧仿真（处理命令、生成 CFG_REPORT、周期推 DATA_SAMPLE）
- `ina228_model.{h,cpp}`：生成模拟测量值（波形/随机游走），输出 raw20


1. `app/`


- `main.cpp`：组装所有组件、配置 link 参数、启动 event loop、输出日志


### 1.2 数据流与控制流


- PC/Device 都通过 `VirtualLinkEndpoint` 发送字节：`endpoint.write(bytes)`
- Event Loop 每 tick：


  1. 调用 `link.pump(now)`：投递到达时间已到的 chunk 到对端 RX buffer
  2. PC/Device 分别执行：


     - `rx = endpoint.read_available()`（非阻塞取出全部或一部分）
     - `parser.feed(rx)` → `on_frame(frame)` 回调处理
  3. 执行定时器（命令超时重传、推流定时发送等）


---


## 2. 关键协议细节（实现必须遵守）


### 2.1 帧格式（LEN 含 MSGID）


帧布局：


```powershell
SOF0=0xAA, SOF1=0x55
VER(1) TYPE(1) FLAGS(1) SEQ(1) LEN(2 LE)
MSGID(1) DATA(N-1)
CRC16(2 LE)   // CRC over VER..DATA_end

```


- `LEN = 1 + DataLen`（包含 MSGID 本身）
- CRC16：CRC16-CCITT-FALSE（poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0）


### 2.2 CRC fail 行为


- **任何帧 CRC fail：静默丢弃**（不回 RSP(ERR_CRC)）
- CMD 超时 → PC 重传解决


### 2.3 SEQ 规则（序号空间隔离）


- CMD：PC 维护 `cmd_seq` 自增
- RSP：设备回显对应 CMD 的 SEQ
- DATA/EVT：设备维护 `data_seq` 自增（mod 256），PC 用于丢帧检测


---


## 3. Virtual Link（串口仿真层）规格


### 3.1 Endpoint API


- `write(ByteVec bytes)`：写入发送队列（进入链路仿真）
- `read(size_t max_bytes)`：读取已到达的 RX bytes
- `available()`：RX bytes 数量


### 3.2 LinkConfig（故障注入参数）


每个方向独立一份（PC→DEV / DEV→PC）：


- `min_chunk, max_chunk`：拆包/粘包模拟（一次 write 被拆成多段）
- `min_delay_us, max_delay_us`：每段 chunk 的投递延迟范围
- `drop_prob`：按 chunk 丢弃概率
- `flip_prob`：按字节翻转概率（随机翻转 1 bit）


建议默认值（调试 parser）：


- `min_chunk=1, max_chunk=16`
- `min_delay_us=0, max_delay_us=2000`
- `drop_prob=0.0`
- `flip_prob=0.0`


---


## 4. Parser 状态机实现要求


### 4.1 输入输出


- 输入：任意分段到来的 bytes（可能半帧/多帧/噪声）
- 输出：校验通过的 Frame：


  - `ver,type,flags,seq,len,msgid,data[]`


### 4.2 必须具备的鲁棒性


- 能处理粘包/拆包
- 能从噪声中重同步（扫描 `AA 55`）
- `LEN` 必须做上限检查：`LEN==0` 或 `LEN>MAX_LEN` 直接丢弃并 resync


  - `MAX_LEN` 建议 1024（或 256，按项目需求）


---


## 5. Device 侧仿真行为（必须实现）


### 5.1 维护的状态


- `config_reg`、`adc_config_reg`、`shunt_cal_reg`、`shunt_tempco`（可选）
- `current_lsb_nA`（由 SET_CFG 设置或固定）
- `flags`：


  - `streaming_on`
  - `cal_valid`（shunt_cal_reg != 0 视为 true）
  - `adcrange`（由 config 或固定）
- streaming：


  - `stream_period_us`
  - `stream_mask`
  - `next_sample_due_us`
- 设备 data 序号：`data_seq`


### 5.2 命令处理


#### PING (0x01)


- 回 `RSP(OK)`（可附带固件版本信息，可选）


#### SET_CFG (0x10)


- 解析 payload（按协议定义：config_reg/adc_config_reg/shunt_cal/shunt_tempco）
- 更新内部状态
- 立即回：`RSP(orig=0x10, status=OK)`
- 紧接着发送：`EVT CFG_REPORT(0x91)`


#### GET_CFG (0x11)


- 回 `RSP(OK)`（无额外 data）
- 紧接着发送：`EVT CFG_REPORT(0x91)`


#### STREAM_START (0x30)


- 解析：`period_us, channel_mask`
- 更新：`stream_period_us`, `stream_mask`, `streaming_on=true`
- 回 `RSP(OK)`
- 设置：`next_sample_due_us = now_us + stream_period_us`


#### STREAM_STOP (0x31)


- `streaming_on=false`
- 回 `RSP(OK)`


---


## 6. DATA_SAMPLE 生成（波形/随机游走）


### 6.1 输出格式（raw 20-bit）


DATA_SAMPLE payload（固定 16 bytes）：


- `timestamp_us (u32 LE)`：从 STREAM_START 执行时刻清零后累加（回绕允许）
- `flags (u8)`：CNVRF/ALERT/CAL_VALID/OVF 等（仿真可简化）
- `vbus20[3]`：u20 LE-packed（寄存器>>4 的 20-bit 值）
- `vshunt20[3]`：s20 LE-packed
- `current20[3]`：s20 LE-packed
- `dietemp16 (i16 LE)`


### 6.2 波形建议（默认实现）


- `vbus_V`：12.0V + 0.1V*sin(2π*0.5Hz*t) + 小噪声
- `current_A`：0.5A + 0.2A*sin(2π*0.8Hz*t + phase) + 小噪声
- `temp_C`：35C + 随机游走（很小步长）


### 6.3 工程值 → raw20 的换算（设备侧仿真）


- VBUS raw20：


  - LSB = 195.3125 µV/LSB
  - `raw = clamp(round(vbus_V / 195.3125e-6), 0..0xFFFFF)`
- CURRENT raw20（依赖 current_lsb_nA）：


  - `CURRENT_A = signed_raw * (current_lsb_nA * 1e-9)`
  - `signed_raw = clamp(round(current_A / (current_lsb_nA*1e-9)), -524288..524287)`
- VSHUNT raw20（依赖 adcrange）：


  - adcrange=0: 312.5nV/LSB
  - adcrange=1: 78.125nV/LSB
  - `signed_raw = clamp(round(vshunt_V / lsb), -524288..524287)`
  - vshunt_V 可由 `current_A * rshunt` 得到（rshunt 可固定一个值，比如 10mΩ），或独立波形
- DIETEMP raw16：


  - LSB = 7.8125 m°C/LSB
  - `raw16 = clamp(round(temp_C / 0.0078125), -32768..32767)`


### 6.4 LE-packed 20-bit 编码


给 agents 一个明确实现：


- 输入：`uint32_t u20`（0..0xFFFFF）或 `int32_t s20`（范围 [-524288, 524287]）
- 输出：


  - `buf[0] = raw & 0xFF`
  - `buf[1] = (raw >> 8) & 0xFF`
  - `buf[2] = (raw >> 16) & 0x0F`（仅低 4 bits 有效）


>
> 对有符号数：先把 s20 转为 20-bit 二补码表示（`raw = (uint32_t)(s20 & 0xFFFFF)`）
>
>
>


---


## 7. PC 侧行为（建议实现）


### 7.1 PC 协议层职责


- 维护 cmd_seq，自增发送 CMD
- 维护 outstanding 命令表（seq→(msgid, deadline, retries, bytes)）
- 收到 RSP：


  - 校验 `orig_msgid` 与 outstanding 匹配
  - 取消超时
- 超时重传：


  - same SEQ 重发，最多 3 次
- 收到 CFG_REPORT：


  - 保存：`current_lsb_nA`, `adcrange`, `stream_period_us`, `stream_mask`
- 收到 DATA_SAMPLE：


  - 记录 DATA SEQ 丢帧统计
  - 可选换算工程值并打印


### 7.2 建议的 demo 场景（main）


1. PC 发送 `PING`
2. PC 发送 `SET_CFG`（给一个 current_lsb_nA 对应的 shunt_cal）
3. PC 发送 `STREAM_START(period_us=1000, mask=0x000F)`
4. 跑 10 秒：打印丢帧率、平均电压电流、CRC fail 计数（如果启用 flip）
5. PC 发送 `STREAM_STOP`


---


## 8. 日志与可观测性要求


必须输出以下日志/统计：


- 每种 MSGID 的收发计数
- CRC fail 计数（两端 parser）
- DATA 丢帧计数（PC）
- 命令超时与重传次数（PC）
- 可选：hex dump（开关控制，避免刷屏）


---


## 9. 交付物与验收标准


### 9.1 交付物


- 可编译运行的 C++17/20 工程（建议 CMake）
- 运行示例：一键启动后自动完成 demo 场景
- README：


  - 如何编译运行
  - 如何调整 link 故障注入参数
  - 如何改变波形参数（电压/电流/温度）


### 9.2 验收标准


- 在 `drop_prob=0, flip_prob=0` 下：


  - PC 能稳定收到 CFG_REPORT
  - DATA_SAMPLE 频率接近 period_us（允许调度误差）
  - 解析无错误、无丢帧（或极少，取决于 event loop tick）
- 在 `min_chunk=1,max_chunk=8` 下：


  - parser 仍能正确切帧（无崩溃、无卡死）
- 在 `flip_prob>0` 下：


  - CRC fail 被统计但不会导致 parser 失同步（会恢复）
  - CMD 超时重传可恢复通信


---

