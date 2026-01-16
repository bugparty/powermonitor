# INA228 USB-TTL Serial Communication Protocol Specification

|||
|---|---|
|**Property**|**Content**|
|**Version**|v1.0 (Draft)|
|**Target Device**|Single INA228 Power Monitor Embedded Device|
|**Communication Link**|USB CDC (Virtual Serial) / UART|

## 1. Protocol Overview

This protocol is designed for high-speed sampling and configuration interaction between PC host and embedded devices.

### 1.1 Core Design Principles

- **Architecture**: Binary frame-based Request-Response model and asynchronous Streaming model.
    
- **Endianness**:
    
    - **Standard Integers** (`u16`, `u32`, `i16`): **Little-Endian**.
        
    - **20-bit Measurement Values**: Non-aligned data, stored as **LE-packed 3-byte** (low byte first).
        
- **Reliability**:
    
    - Control commands: With sequence number (SEQ) and acknowledgment (ACK/RSP).
        
    - Data stream: No ACK, relies on sequence number continuity to detect packet loss.
        

## 2. Frame Structure

Uses classic structure of **delimiter + header + payload + checksum**.

### 2.1 Frame Definition

|Offset|Field|Length|Description|Value/Range|
|---|---|---|---|---|
|0|**SOF0**|1|Start of Frame 0|`0xAA`|
|1|**SOF1**|1|Start of Frame 1|`0x55`|
|2|**VER**|1|Protocol Version|`0x01`|
|3|**TYPE**|1|Frame Type|See 2.2|
|4|**FLAGS**|1|Flag Bits|Bit0: ACK_REQ, Bit1: ACK|
|5|**SEQ**|1|Frame Sequence Number|0~255 (circular increment)|
|6|**LEN_L**|1|Payload Length Low Byte|N = 1 + DataLen|
|7|**LEN_H**|1|Payload Length High Byte|(includes MSGID)|
|8|**MSGID**|1|Message ID|See Chapter 3|
|9|**DATA**|N-1|Data Payload|Length = LEN - 1|
|8+N|**CRC_L**|1|CRC16 Checksum Low Byte|Algorithm: CRC-16/CCITT-FALSE|
|9+N|**CRC_H**|1|CRC16 Checksum High Byte|Range: `VER` to end of `DATA`|

> **LEN Field Description**: `LEN = sizeof(MSGID) + sizeof(DATA) = 1 + DataLen`
> 
> **CRC Calculation Range**: From `VER`(offset 2) to end of `DATA`(offset 8+N-1), total `6 + N` bytes **CRC Parameters**: Poly=`0x1021`, Init=`0xFFFF`, RefIn=`False`, RefOut=`False`, XorOut=`0x0000`

### 2.2 Frame Type (TYPE)

|||||
|---|---|---|---|
|**TYPE**|**Macro**|**Direction**|**Description**|
|**0x01**|`CMD`|PC → Dev|Control command, usually requires response|
|**0x02**|`RSP`|Dev → PC|Command response, carries execution status|
|**0x03**|`DATA`|Dev → PC|Periodic sampling data stream (no ACK)|
|**0x04**|`EVT`|Dev → PC|Asynchronous event or configuration report|

### 2.3 Sequence Number (SEQ) Usage Rules

|Frame Type|SEQ Behavior|Description|
|---|---|---|
|**CMD**|Sender increments|PC maintains independent CMD sequence, 0→255 loop|
|**RSP**|Echo request SEQ|Device copies SEQ from corresponding CMD when responding|
|**DATA**|Device increments independently|Device maintains independent DATA sequence space for packet loss detection|
|**EVT**|Device increments independently|Shares sequence space with DATA|

**Packet Loss Detection Mechanism**:

- PC should record the SEQ of last received DATA frame
- If current SEQ ≠ (last SEQ + 1) % 256, packet loss is detected
- PC can log or request retransmission on packet loss (not enforced by protocol)

**Sequence Space Isolation**:

- CMD/RSP sequence space: PC-driven, for command confirmation
- DATA/EVT sequence space: Device-driven, for stream data integrity detection

## 3. Message ID Registry

||||||
|---|---|---|---|---|
|**MSGID**|**Name**|**Direction**|**Category**|**Description**|
|**0x01**|`PING`|P→D|Management|Heartbeat / Handshake|
|**0x02**|-|D→P|Management|(Reserved, generic response usually reuses CMD ID or independent)|
|**0x10**|`SET_CFG`|P→D|Configuration|Send parameters (triggers CFG_REPORT)|
|**0x11**|`GET_CFG`|P→D|Configuration|Query parameters (triggers CFG_REPORT)|
|**0x20**|`REG_READ`|P→D|Debug|Read INA228 register (supports 16/24/40 bit)|
|**0x21**|`REG_WRITE`|P→D|Debug|Write INA228 register (fixed 16 bit)|
|**0x30**|`STREAM_START`|P→D|Stream Control|Start streaming|
|**0x31**|`STREAM_STOP`|P→D|Stream Control|Stop streaming|
|**0x80**|`DATA_SAMPLE`|D→P|Data|Core sampling data (voltage/current raw values)|
|**0x90**|`EVT_ALERT`|D→P|Event|Hardware alert (Over-Voltage, etc.)|
|**0x91**|`CFG_REPORT`|D→P|Event|**Configuration report** (includes conversion coefficients)|

## 4. Common Response Mechanism

All CMD frames require RSP response, except DATA frames.

### 4.1 Common RSP Payload Structure

When TYPE = `0x02` (RSP):

```c
struct {
    uint8_t orig_msgid;  // MSGID of corresponding request
    uint8_t status;      // Execution status code
    uint8_t data[];      // (Optional) Additional return data, depends on specific command
} __attribute__((packed));
```

### 4.2 Status Code Definitions

||||
|---|---|---|
|**Value**|**Name**|**Description**|
|`0x00`|**OK**|Success|
|`0x01`|**ERR_CRC**|CRC verification failed (usually silently dropped, only reply in debug mode)|
|`0x02`|**ERR_LEN**|Invalid packet length|
|`0x03`|**ERR_UNK_CMD**|Unknown or unsupported MSGID|
|`0x04`|**ERR_PARAM**|Parameter out of range|
|`0x05`|**ERR_HW**|Hardware fault (e.g., I2C NACK)|

### 4.3 Error Handling Mechanism

#### 4.3.1 CRC Verification Failure

|Frame Type|Handling|Description|
|---|---|---|
|**DATA (0x03)**|Silently drop|High-frequency data stream, no reply to avoid congestion|
|**CMD (0x01)**|Reply `RSP(ERR_CRC)`|Notify PC to resend|
|**EVT (0x04)**|Silently drop|Device event, cannot request retransmission|

#### 4.3.2 Invalid Frame Length

|Frame Type|Handling|
|---|---|
|**DATA**|Silently drop|
|**CMD**|Reply `RSP(ERR_LEN)`|

#### 4.3.3 Unknown MSGID

|Frame Type|Handling|
|---|---|
|**DATA**|Silently drop|
|**CMD**|Reply `RSP(ERR_UNK_CMD)`|

#### 4.3.4 Timeout and Retransmission (Recommended Implementation)

This protocol does not enforce retransmission mechanism, but PC-side implementation is recommended:

```
CMD Timeout Handling:
1. Start timer after sending CMD (recommended 100~500ms)
2. Cancel timer upon receiving RSP with corresponding SEQ
3. Timeout without receiving RSP:
   - Retransmit same CMD (keep SEQ unchanged), max 3 retries
   - Report communication error after 3 failures

DATA Packet Loss Handling:
1. Record packet loss count when SEQ discontinuity detected
2. Reduce sampling frequency or check physical link when packet loss rate is too high
```

#### 4.3.5 Status Code Quick Reference

|Status Code|Trigger Condition|PC-side Recommended Action|
|---|---|---|
|`0x00 OK`|Command success|Process response data normally|
|`0x01 ERR_CRC`|CRC verification failed|Resend command|
|`0x02 ERR_LEN`|Invalid packet length|Check packet assembly logic|
|`0x03 ERR_UNK_CMD`|Unsupported MSGID|Check protocol version compatibility|
|`0x04 ERR_PARAM`|Parameter out of bounds|Check parameter range|
|`0x05 ERR_HW`|Hardware fault (I2C NACK, etc.)|Check hardware connection|

## 5. Detailed Payload Definitions

### 5.1 Data Stream & Configuration (Core & Config)

#### 5.1.1 DATA_SAMPLE (0x80)

- **Direction**: Dev → PC
- **Frame Type**: `DATA (0x03)`
- **Description**: Core high-frequency data. Transmits **raw register values** (Register >> 4), no floating-point calculation.

```c
struct DataSample {
    uint32_t timestamp_us;   // Relative timestamp (microseconds), see explanation below

    uint8_t  flags;          // Status flags:
                             //   Bit0: CNVRF  - ADC conversion complete
                             //   Bit1: ALERT  - Alert triggered
                             //   Bit2: CAL_VALID - Calibration valid (0 means CURRENT invalid)
                             //   Bit3: OVF    - Math overflow

    uint8_t  vbus20[3];      // VBUS unsigned 20-bit LE-packed
    uint8_t  vshunt20[3];    // VSHUNT signed 20-bit LE-packed
    uint8_t  current20[3];   // CURRENT signed 20-bit LE-packed
    int16_t  dietemp16;      // DIE_TEMP signed 16-bit

} __attribute__((packed));   // Total length: 4 + 1 + 3 + 3 + 3 + 2 = 16 bytes
```

**timestamp_us Description**:

|Feature|Description|
|---|---|
|**Origin**|Moment when `STREAM_START` command is executed, resets to 0|
|**Precision**|1 microsecond|
|**Range**|0 ~ 4,294,967,295 µs (approximately 71.6 minutes)|
|**Overflow Handling**|Natural wrap-around to 0, PC should detect and accumulate overflow count|

**PC-side Time Reconstruction Algorithm**:

```c
// State variables
uint32_t last_ts = 0;
uint64_t overflow_count = 0;

// Each time a frame is received
void on_data_sample(uint32_t ts_us) {
    if (ts_us < last_ts) {
        // Wrap-around detected
        overflow_count++;
    }
    last_ts = ts_us;
    
    // Absolute time (microseconds)
    uint64_t absolute_us = (overflow_count << 32) + ts_us;
}
```

> **Design Notes**: Benefits of using relative timestamp vs absolute timestamp:
> 
> 1. Device doesn't need RTC or NTP synchronization
> 2. Time precision guaranteed by device local timer
> 3. PC can convert to local time as needed

#### 5.1.2 CFG_REPORT (0x91)

- **Direction**: Dev → PC
- **Frame Type**: `EVT (0x04)`
- **Trigger Conditions and Response Flow**:

|Trigger Scenario|Interaction Flow|
|---|---|
|Device Power-on|Device sends `CFG_REPORT` actively after initialization|
|`SET_CFG` Success|`RSP(OK)` → followed by `CFG_REPORT`|
|`GET_CFG` Request|`RSP(OK)` → followed by `CFG_REPORT`|
|Configuration Change (internal trigger)|Send `CFG_REPORT` actively|

> **Description**: Both `GET_CFG` and `SET_CFG` first reply with `RSP` to confirm command received, then return complete configuration through `CFG_REPORT`. This design benefits:
> 
> 1. Maintains response model consistency (all CMD have RSP)
> 2. `CFG_REPORT` format is unified, PC only needs one parsing logic
> 
> **Protocol Core**: Tells host how to convert RAW data to physical quantities.

```c
struct {
    uint8_t  proto_ver;       // Protocol version (0x01)
    
    uint8_t  flags;           // Bit0: streaming_on
                              // Bit1: cal_valid
                              // Bit2: adcrange (0=312.5nV, 1=78.125nV)

    uint32_t current_lsb_nA;  // Critical: Current LSB, unit nA (integer)
    
    uint16_t shunt_cal_reg;   // Actual SHUNT_CAL register value written (for reconciliation)
    uint16_t config_reg;      // Actual CONFIG
    uint16_t adc_config_reg;  // Actual ADC_CONFIG
    
    uint16_t stream_period_us;// Current streaming period
    uint16_t stream_mask;     // Streaming channel mask
} __attribute__((packed));
```

#### 5.1.3 SET_CFG (0x10)

- **Direction**: PC → Dev

```c
struct {
    uint16_t config_reg;      // INA228 CONFIG
    uint16_t adc_config_reg;  // INA228 ADC_CONFIG
    uint16_t shunt_cal;       // Target SHUNT_CAL
    uint16_t shunt_tempco;    // (Optional) Temperature coefficient
} __attribute__((packed));
```

- **Response**: First reply `RSP(OK)`, immediately followed by `CFG_REPORT(0x91)`.

#### 5.1.4 STREAM_START (0x30)

- **Direction**: PC → Dev

**CMD Payload**:

```c
struct StreamStartCmd {
    uint16_t period_us;      // Sampling period (microseconds), minimum depends on ADC configuration
    uint16_t channel_mask;   // Channel enable mask, see table below
} __attribute__((packed));
```

**channel_mask Bit Definitions**:

|Bit|Channel|Description|
|---|---|---|
|0|VBUS|Bus voltage|
|1|VSHUNT|Shunt voltage|
|2|CURRENT|Current (requires CAL_VALID)|
|3|DIETEMP|Die temperature|
|4~15|Reserved|Set to 0|

**Examples**:

- `0x000F`: Enable all 4 channels
- `0x0005`: Only VBUS + CURRENT
- `0x0001`: Only VBUS

**Response**: `RSP(OK)` then start pushing `DATA_SAMPLE`

> **Note**:
> 
> - `period_us` should not be less than ADC conversion time, otherwise device will push at actual fastest speed
> - Disabled channels still occupy space in `DATA_SAMPLE` (filled with 0), maintaining fixed frame length

#### 5.1.5 GET_CFG (0x11)

- **Direction**: PC → Dev
- **Payload**: Empty (LEN = 1, only contains MSGID)

```c
// No additional parameters, only requests device to report current configuration
```

- **Response**: `RSP(OK)` + `CFG_REPORT(0x91)`

### 5.2 Debug Commands

#### 5.2.1 REG_READ (0x20)

- **Direction**: PC → Dev
- **Purpose**: Read INA228 register raw value (supports 16/24/40-bit registers)

**CMD Payload**:

```c
struct RegReadCmd {
    uint8_t ina_addr;   // INA228 I2C address (usually 0x40~0x4F)
    uint8_t reg_addr;   // Register address (0x00~0x0F)
    uint8_t reg_type;   // Register bit width type:
                        //   0 = 16-bit (CONFIG, SHUNT_CAL, etc.)
                        //   1 = 24-bit (VSHUNT, VBUS, CURRENT, POWER)
                        //   2 = 40-bit (ENERGY, CHARGE)
} __attribute__((packed));
```

**RSP Payload** (when Status = OK):

```c
struct RegReadRsp {
    uint8_t orig_msgid; // = 0x20
    uint8_t status;     // = 0x00 (OK)
    uint8_t reg_addr;   // Confirmed register address read
    uint8_t value[];    // Variable length data, Little Endian:
                        //   reg_type=0: 2 bytes
                        //   reg_type=1: 3 bytes
                        //   reg_type=2: 5 bytes
} __attribute__((packed));
```

**Register Bit Width Reference Table**:

|reg_type|Bit Width|Applicable Registers|
|---|---|---|
|0|16-bit|CONFIG (0x00), ADC_CONFIG (0x01), SHUNT_CAL (0x02), SHUNT_TEMPCO (0x03), DIAG_ALRT (0x0B), SOVL/SUVL/BOVL/BUVL/TEMP_LIMIT/PWR_LIMIT (0x0C~0x11), MANUFACTURER_ID (0x3E), DEVICE_ID (0x3F)|
|1|24-bit|VSHUNT (0x04), VBUS (0x05), DIETEMP (0x06), CURRENT (0x07), POWER (0x08)|
|2|40-bit|ENERGY (0x09), CHARGE (0x0A)|

#### 5.2.2 REG_WRITE (0x21)

- **Description**: INA228's writable registers (Configuration, Calibration, Limits, etc.) are all **16-bit**.
    
- **CMD Payload**:
    

```c
struct {
    uint8_t  ina_addr;
    uint8_t  reg_addr;
    uint16_t reg_value; // Fixed 16-bit write value
} __attribute__((packed));
```

## 6. Data Conversion Guide

After receiving `DATA_SAMPLE`, the host **must** combine parameters from the most recent `CFG_REPORT` to restore physical quantities.

### 6.1 20-bit Data Format Description

INA228's measurement registers (VSHUNT, VBUS, CURRENT, etc.) are 24-bit, where the high 20 bits are valid data and the low 4 bits are reserved.

**Data Source**:

```
Original register value (24-bit) >> 4 = 20-bit packed value
```

**Storage Format**: LE-packed 3 bytes, low byte first

```
Byte layout: [Bit7:0] [Bit15:8] [Bit19:16]
             buf[0]   buf[1]    buf[2]
```

**Sign Bit**: Bit 19 (MSB)

- Unsigned (VBUS): Use directly, range 0 ~ 0xFFFFF
- Signed (VSHUNT, CURRENT): Requires sign extension to 32-bit

### 6.2 20-bit Unpacking Algorithm

```c
// ===== Unsigned unpacking (VBUS) =====
uint32_t unpack_u20(const uint8_t buf[3]) {
    return (uint32_t)buf[0] | 
           ((uint32_t)buf[1] << 8) | 
           ((uint32_t)buf[2] << 16);
}

// ===== Signed unpacking (VSHUNT, CURRENT) =====
int32_t unpack_s20(const uint8_t buf[3]) {
    uint32_t raw = (uint32_t)buf[0] | 
                   ((uint32_t)buf[1] << 8) | 
                   ((uint32_t)buf[2] << 16);
    
    // Bit 19 is sign bit, extend to 32-bit
    if (raw & 0x80000) {
        raw |= 0xFFF00000;  // Sign extension
    }
    return (int32_t)raw;
}
```

### 6.3 Physical Quantity Calculation

|Physical Quantity|Data Type|Formula|Dependent Parameters|
|---|---|---|---|
|**Voltage (VBUS)**|Unsigned 20-bit|`V = unpack_u20(vbus20) * 195.3125e-6`|Fixed LSB = 195.3125 µV|
|**Shunt Voltage (VSHUNT)**|Signed 20-bit|`V = unpack_s20(vshunt20) * LSB_VSHUNT`|`adcrange=0`: 312.5 nV<br>`adcrange=1`: 78.125 nV|
|**Current (CURRENT)**|Signed 20-bit|`I = unpack_s20(current20) * current_lsb_nA * 1e-9`|`current_lsb_nA` from CFG_REPORT|
|**Temperature (DIE_TEMP)**|Signed 16-bit|`T = (int16_t)dietemp16 * 7.8125e-3`|Fixed LSB = 7.8125 m°C|

> **Note**: When `CFG_REPORT.flags.CAL_VALID = 0`, CURRENT value is invalid, host should ignore or display as N/A.

## 7. Sequence Diagrams

### 7.1 System Startup and Sampling Flow

```mermaid
sequenceDiagram
    participant PC as Host (PC)
    participant Dev as Device
    
    Note over PC, Dev: 1. Connection Handshake
    PC->>Dev: CMD: PING (Seq=1)
    Dev->>PC: RSP: OK (Seq=1)

    Note over PC, Dev: 2. Parameter Configuration
    PC->>Dev: CMD: SET_CFG (Seq=2, Payload=...)
    Dev->>PC: RSP: OK (Seq=2)
    Dev-->>PC: EVT: CFG_REPORT (MsgID=0x91)
    Note right of PC: PC saves current_lsb_nA\nfor subsequent conversion

    Note over PC, Dev: 3. Start Data Stream
    PC->>Dev: CMD: STREAM_START (Seq=3)
    Dev->>PC: RSP: OK (Seq=3)
    
    loop Every 1ms
        Dev->>PC: DATA: SAMPLE (MsgID=0x80)
        Note right of PC: I = Raw * LSB
    end

    Note over PC, Dev: 4. Stop
    PC->>Dev: CMD: STREAM_STOP (Seq=4)
    Dev->>PC: RSP: OK (Seq=4)
```

# Appendix

PC-side Parser State Machine

```mermaid
stateDiagram-v2
    [*] --> WAIT_SOF0: Start

    state "WAIT_SOF0 (0xAA)" as WAIT_SOF0
    state "WAIT_SOF1 (0x55)" as WAIT_SOF1
    state "READ_HEADER" as READ_HEADER
    state "READ_PAYLOAD" as READ_PAYLOAD
    state "VERIFY_CRC" as VERIFY_CRC
	state "RESYNC" as RESYNC
	
    WAIT_SOF0 --> WAIT_SOF1: Recv 0xAA
    WAIT_SOF0 --> WAIT_SOF0: Recv Other (Drop)

    WAIT_SOF1 --> READ_HEADER: Recv 0x55
    WAIT_SOF1 --> WAIT_SOF0: Recv Other (Reset)
    WAIT_SOF1 --> WAIT_SOF1: Recv 0xAA Stay (Treat as new SOF0)

    READ_HEADER --> READ_PAYLOAD: Got 6 Bytes (Get LEN)
    READ_HEADER --> WAIT_SOF0: Header Timeout/Err

    READ_PAYLOAD --> VERIFY_CRC: Got (LEN + 2) Bytes
    READ_PAYLOAD --> WAIT_SOF0: Payload Timeout

    VERIFY_CRC --> WAIT_SOF0: CRC OK (Dispatch Frame)
    VERIFY_CRC --> RESYNC: CRC Fail 
    RESYNC --> READ_HEADER: Found AA 55 in buffer 
    RESYNC --> WAIT_SOF0: No AA 55 found
```

# implenment plan INA228 协议仿真器（PC 端）实现文档 v0.1

下面是一份可以直接丢给 agents 开工的**实现文档 / 任务规格**（面向“单进程事件循环 + 虚拟串口链路 + 设备侧仿真 + PC 侧协议栈”）。我按你最新协议（LEN 含 MSGID、CRC16-CCITT-FALSE、DATA/EVT 序号空间等）来写。


---


# INA228 协议仿真器（PC 端）实现文档 v0.1


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

