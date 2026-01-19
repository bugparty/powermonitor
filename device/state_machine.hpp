#ifndef DEVICE_STATE_MACHINE_HPP
#define DEVICE_STATE_MACHINE_HPP

#include <cstdint>

// Define PHASE2_DUAL_CORE to enable dual-core mode with real I2C sampling
// Comment out for Phase 1 (single-core with fake data)
#define PHASE2_DUAL_CORE

#ifndef PHASE2_DUAL_CORE
#include "core/fake_data.hpp"
#endif

#ifdef PHASE2_DUAL_CORE
#include "core/shared_context.hpp"
#include "hardware/sync.h"
#endif

// Forward declaration
class INA228;

namespace device {

// Device states
enum class DeviceState {
    kIdle,       // Not streaming, waiting for commands
    kStreaming,  // Actively sending DATA_SAMPLE frames
};

// Device context - holds all runtime state
struct DeviceContext {
    // Hardware drivers
    INA228* ina228 = nullptr;

    // Current state
    DeviceState state = DeviceState::kIdle;

    // Sequence numbers
    uint8_t data_seq = 0;   // For DATA/EVT frames (device-driven)

    // Streaming configuration
    uint16_t stream_period_us = 1000;   // Default 1kHz
    uint16_t stream_mask = 0x000F;      // All channels enabled
    uint32_t stream_start_us = 0;       // Timestamp when streaming started

    // INA228 configuration (from SET_CFG)
    uint16_t config_reg = 0;
    uint16_t adc_config_reg = 0;
    uint16_t shunt_cal = 0;
    uint16_t shunt_tempco = 0;
    uint32_t current_lsb_nA = 1000;     // Default 1uA/LSB

    // Status flags
    bool cal_valid = false;             // SHUNT_CAL has been set
    uint8_t adcrange = 0;               // 0: ±163.84mV, 1: ±40.96mV

#ifndef PHASE2_DUAL_CORE
    // Phase 1: Fake data generator
    core::FakeDataGenerator fake_gen;
#endif

#ifdef PHASE2_DUAL_CORE
    // Phase 2: Pointer to shared context for inter-core communication
    core::SharedContext* shared_ctx = nullptr;
#endif

    // Statistics
    uint32_t samples_sent = 0;
    uint32_t cmd_received = 0;

    // Helper: check if streaming
    bool is_streaming() const {
        return state == DeviceState::kStreaming;
    }

    // Start streaming (Phase 1: just set state, Phase 2: update shared context)
    void start_streaming(uint16_t period_us, uint16_t mask, uint32_t now_us) {
        stream_period_us = period_us;
        stream_mask = mask;
        stream_start_us = now_us;
        samples_sent = 0;
        state = DeviceState::kStreaming;

#ifndef PHASE2_DUAL_CORE
        fake_gen.reset();
#endif

#ifdef PHASE2_DUAL_CORE
        // Update shared context for Core 1
        if (shared_ctx) {
            shared_ctx->stream_period_us = period_us;
            shared_ctx->stream_mask = mask;
            shared_ctx->stream_start_us = now_us;

            // Ensure memory writes are visible to Core 1 before we signal it.
            __dmb();
        }
#endif
    }

    // Stop streaming
    void stop_streaming() {
        state = DeviceState::kIdle;
    }

    // Get next data sequence number and increment
    uint8_t next_data_seq() {
        return data_seq++;
    }
};

} // namespace device

#endif // DEVICE_STATE_MACHINE_HPP
