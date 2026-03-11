#ifndef DEVICE_PROTOCOL_FRAME_DEFS_HPP
#define DEVICE_PROTOCOL_FRAME_DEFS_HPP

#include <cstdint>

namespace protocol {

// Start of Frame bytes
constexpr uint8_t kSof0 = 0xAA;
constexpr uint8_t kSof1 = 0x55;

// Protocol version
constexpr uint8_t kProtoVersion = 0x01;

// Maximum payload length for RECEIVING frames (LEN field, includes MSGID).
// Device only receives CMD frames from PC, which are small (max ~10 bytes).
// Keep this small to avoid large stack/BSS allocations on RP2040.
constexpr uint16_t kMaxPayloadLen = 256;

// Maximum payload length for SENDING frames (e.g. TEXT_REPORT up to 4096 bytes).
// LEN = MSGID(1) + text(4096) = 4097.
constexpr uint16_t kMaxTxPayloadLen = 1025;

// Frame header size (VER + TYPE + FLAGS + SEQ + LEN)
constexpr size_t kHeaderSize = 6;

// Frame type (TYPE field)
enum class FrameType : uint8_t {
    kCmd  = 0x01,  // PC -> Device: Control command
    kRsp  = 0x02,  // Device -> PC: Command response
    kData = 0x03,  // Device -> PC: Data stream (no ACK)
    kEvt  = 0x04,  // Device -> PC: Async event
};

// Message IDs (MSGID field)
enum class MsgId : uint8_t {
    // Management
    kPing        = 0x01,
    kTimeSync    = 0x05,
    kTimeAdjust  = 0x06,
    kTimeSet     = 0x07,

    // Configuration
    kSetCfg      = 0x10,
    kGetCfg      = 0x11,

    // Debug
    kRegRead     = 0x20,
    kRegWrite    = 0x21,

    // Stream control
    kStreamStart = 0x30,
    kStreamStop  = 0x31,

    // Data
    kDataSample  = 0x80,

    // Events
    kEvtAlert    = 0x90,
    kCfgReport   = 0x91,
    kStatsReport = 0x92,
    kTextReport      = 0x93,
    kTimeSyncRequest = 0x94,
};

// Response status codes
enum class Status : uint8_t {
    kOk         = 0x00,  // Success
    kErrCrc     = 0x01,  // CRC verification failed
    kErrLen     = 0x02,  // Invalid packet length
    kErrUnkCmd  = 0x03,  // Unknown or unsupported MSGID
    kErrParam   = 0x04,  // Parameter out of range
    kErrHw      = 0x05,  // Hardware fault (e.g., I2C NACK)
};

// Parsed frame structure (fixed-size, no dynamic allocation)
struct Frame {
    uint8_t ver;
    FrameType type;
    uint8_t flags;
    uint8_t seq;
    uint16_t len;          // Payload length (includes MSGID)
    uint8_t msgid;
    uint8_t data[kMaxPayloadLen];  // Payload data (excluding MSGID, sized for receive)
    uint16_t data_len;     // Actual data length (len - 1)
};

// DATA_SAMPLE payload structure (24 bytes, packed).
// Field order is the wire/protocol order: timestamp_unix_us (8), timestamp_us (4),
// flags (1), vbus20[3], vshunt20[3], current20[3], then dietemp16 (int16_t) last.
// Because this struct is packed, do not apply manual alignment-based reordering.
struct DataSamplePayload {
    uint64_t timestamp_unix_us;  // Absolute Unix timestamp in microseconds
    uint32_t timestamp_us;        // Relative timestamp since STREAM_START
    uint8_t flags;               // Bit0: CNVRF, Bit1: ALERT, Bit2: CAL_VALID, Bit3: OVF
    uint8_t vbus20[3];           // VBUS unsigned 20-bit LE-packed
    uint8_t vshunt20[3];         // VSHUNT signed 20-bit LE-packed
    uint8_t current20[3];        // CURRENT signed 20-bit LE-packed
    uint8_t power24[3];          // POWER unsigned 24-bit LE-packed
    int16_t dietemp16;           // DIE_TEMP signed 16-bit
    uint8_t energy40[5];         // ENERGY unsigned 40-bit LE-packed
    uint8_t charge40[5];         // CHARGE signed 40-bit LE-packed
} __attribute__((packed));

static_assert(sizeof(DataSamplePayload) == 37, "DataSamplePayload must be 37 bytes");

// CFG_REPORT payload structure
struct CfgReportPayload {
    uint8_t proto_ver;       // Protocol version (0x01)
    uint8_t flags;           // Bit0: streaming_on, Bit1: cal_valid, Bit2: adcrange
    uint32_t current_lsb_nA; // Current LSB in nA
    uint16_t shunt_cal_reg;  // SHUNT_CAL register value
    uint16_t config_reg;     // CONFIG register value
    uint16_t adc_config_reg; // ADC_CONFIG register value
    uint16_t stream_period_us;
    uint16_t stream_mask;
} __attribute__((packed));

static_assert(sizeof(CfgReportPayload) == 16, "CfgReportPayload must be 16 bytes");

// STATS_REPORT payload structure
struct StatsReportPayload {
    uint16_t report_seq;        // Monotonic report sequence (wrap-around)
    uint32_t samples_produced;  // Total samples produced on device side
    uint32_t samples_dropped;   // Total dropped samples on device side
    uint32_t dropped_cnvrf_not_ready;
    uint32_t dropped_duplicate_suppressed;
    uint32_t dropped_worker_missed_tick;
    uint32_t dropped_queue_full;
    uint16_t queue_depth;       // Queue depth snapshot when report is generated
    uint8_t reason_bits;        // Reserved for future reason breakdown
    uint16_t window_ms;         // Reporting window in milliseconds
} __attribute__((packed));

static_assert(sizeof(StatsReportPayload) == 31, "StatsReportPayload must be 31 bytes");

// SET_CFG command payload
struct SetCfgPayload {
    uint16_t config_reg;
    uint16_t adc_config_reg;
    uint16_t shunt_cal;
    uint16_t shunt_tempco;
} __attribute__((packed));

// STREAM_START command payload
struct StreamStartPayload {
    uint16_t period_us;
    uint16_t channel_mask;
} __attribute__((packed));

// REG_READ command payload
struct RegReadCmdPayload {
    uint8_t ina_addr;
    uint8_t reg_addr;
    uint8_t reg_type;  // 0=16-bit, 1=24-bit, 2=40-bit
} __attribute__((packed));

// REG_WRITE command payload
struct RegWriteCmdPayload {
    uint8_t ina_addr;
    uint8_t reg_addr;
    uint16_t reg_value;
} __attribute__((packed));

// TIME_SYNC command payload
struct TimeSyncPayload {
    uint64_t t1;
} __attribute__((packed));

// TIME_SYNC response payload
struct TimeSyncResponsePayload {
    uint8_t orig_msgid;
    uint8_t status;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
} __attribute__((packed));

// TIME_ADJUST command payload
struct TimeAdjustPayload {
    int64_t offset_us;
} __attribute__((packed));

// TIME_SET command payload
struct TimeSetPayload {
    uint64_t unix_time_us;
} __attribute__((packed));

} // namespace protocol

#endif // DEVICE_PROTOCOL_FRAME_DEFS_HPP
