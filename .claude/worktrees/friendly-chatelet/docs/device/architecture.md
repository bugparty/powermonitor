# Device Firmware Architecture

This document describes the dual-core architecture for the Power Monitor device firmware on Raspberry Pi Pico (RP2040).

## Overview

The firmware uses both RP2040 cores to separate concerns:
- **Core 0**: USB CDC communication and protocol processing.
- **Core 1**: Real-time sensor sampling, coordinated by a timer and a low-power worker loop.

Data flows from Core 1 to Core 0 through a lock-free Single-Producer, Single-Consumer (SPSC) queue.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                                  RP2040                                     │
│  ┌──────────────────────────┐         ┌───────────────────────────────────┐  │
│  │          Core 1          │         │              Core 0               │  │
│  │                          │         │                                   │  │
│  │  ┌────────────────────┐  │         │  ┌─────────────────────────────┐  │  │
│  │  │ Timer ISR (e.g. 1kHz)│  │         │  │ Main Loop                   │  │  │
│  │  │   - inc isr_seq      │────SEV─────>│  │                             │  │  │
│  │  │   - __sev()          │  │         │  │ ┌─────────────────────────┐ │  │  │
│  │  └────────────────────┘  │         │  │ │ USB CDC RX/TX             │ │  │  │
│  │           ▲              │         │  │ │ Protocol Parser           │ │  │  │
│  │           │              │         │  │ │ State Machine & Cmd Handler │ │  │  │
│  │  ┌────────────────────┐  │         │  │ └─────────────────────────┘ │  │  │
│  │  │ Worker Loop (__wfe())│<──────────WFE │              │              │  │  │
│  │  │  - Read INA228 (I2C) │  │         │  │              ▼              │  │  │
│  │  │  - Push to Queue     │  │         │  │ Pop from Queue, send frames │  │  │
│  │  └──────────┬───────────┘  │         │  └─────────────────────────────┘  │  │
│  │             │              │         │                                   │  │
│  └─────────────┼──────────────┘         └───────────────────────────────────┘  │
│                │                                                            │  │
│                ▼                                                            │  │
│  ┌──────────────────────────┐                                             │  │
│  │   Sample SPSC Queue      │◄─────────────────────────────────────────────┘  │
│  │   (e.g. 256 samples)     │                                                 │
│  └──────────────────────────┘                                                 │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Core Responsibilities

### Core 0 (Main Core)

**Responsibilities:**
- Initialize hardware (GPIO, I2C, USB) and launch Core 1.
- Run TinyUSB task loop for USB communication.
- Receive USB CDC data and feed it to the protocol parser.
- Process parsed commands via the state machine and command handler.
- When streaming: pop samples from the SPSC queue and send `DATA_SAMPLE` frames over USB.
- Send all other responses and events (`RSP`, `CFG_REPORT`, etc.).

**Main Loop:**
```cpp
while (true) {
    tud_task(); // TinyUSB housekeeping

    // Handle incoming commands
    if (tud_cdc_available()) {
        uint8_t buf[64];
        uint32_t count = tud_cdc_read(buf, sizeof(buf));
        parser.feed(buf, count);
    }

    // When streaming: send queued samples
    if (ctx.is_streaming()) {
        process_streaming(); // Pops samples from queue and sends
    }
}
```

### Core 1 (Sampling Core)

**Responsibilities:**
- An efficient, two-stage process for sampling:
    1. A **Timer ISR** runs at the configured rate (e.g., 1kHz). It performs the minimal work of incrementing a sequence counter and issuing a `__sev()` (Send Event) instruction to wake the worker.
    2. A **Worker Loop** spends most of its time in a low-power sleep state, waiting for an event with `__wfe()` (Wait For Event). When woken by the ISR, it performs the I2C reads, packages the data into a `RawSample`, and pushes it to the SPSC queue.
- This design avoids performing slow I2C operations within an ISR context, which is critical for system stability.
- The worker loop also processes commands from Core 0 via the multicore FIFO.

**Core 1 Logic:**
```cpp
// Timer ISR - extremely short
static bool sampler_timer_isr(repeating_timer_t* rt) {
    SamplerContext* ctx = static_cast<SamplerContext*>(rt->user_data);
    ctx->isr_seq++;  // Increment sequence number
    __sev();         // Wake up worker loop
    return true;     // Keep timer repeating
}

// Worker loop - does the heavy lifting
static void core1_entry() {
    while (true) {
        // Handle commands from Core 0 via FIFO
        if (multicore_fifo_rvalid()) {
            uint32_t cmd_raw = multicore_fifo_pop_blocking();
            __dmb();  // Memory barrier

            core::FifoCmd cmd = static_cast<core::FifoCmd>(cmd_raw);
            switch (cmd) {
            case core::FifoCmd::kStartStream:
                // Start repeating timer
                add_repeating_timer_us(-period, sampler_timer_isr, &ctx, &timer);
                timer_active = true;
                break;
            case core::FifoCmd::kStopStream:
                cancel_repeating_timer(&timer);
                timer_active = false;
                break;
            }
        }

        // Do I2C reads when ISR signals new tick
        if (timer_active) {
            sampler_do_work(&g_sampler_ctx);
        }

        // Low-power sleep (woken by __sev() from ISR or FIFO)
        __wfe();
    }
}

// Perform the actual sampling work
static void sampler_do_work(SamplerContext* ctx) {
    // Read current ISR sequence
    uint32_t current_seq = ctx->isr_seq;
    if (current_seq == ctx->worker_seq) {
        return;  // Nothing new to process
    }

    // Update missed ticks count
    uint32_t missed = current_seq - ctx->worker_seq - 1;
    if (missed > 0) {
        ctx->shared->samples_dropped += missed;
    }
    ctx->worker_seq = current_seq;

    // Read sensor data via I2C
    RawSample sample;
    bool ok = ctx->ina228->read_vbus_raw(sample.vbus_raw);
    ok &= ctx->ina228->read_vshunt_raw(sample.vshunt_raw);
    // ... read other channels

    // Push to SPSC queue for Core 0
    ctx->shared->sample_queue.push(sample);
}

void core1_entry() {
    // Core 1 runs the sampling timer
    repeating_timer_t timer;
    add_repeating_timer_us(-stream_period_us, timer_callback, &sampler_ctx, &timer);

    while (true) {
        tight_loop_contents();  // Low-power wait
    }
}
```

## File Structure


```text
device/
├── protocol/                  # Protocol layer (Phase 1)
│   ├── crc16.hpp
│   ├── frame_defs.hpp
│   ├── frame_builder.hpp
│   └── parser.hpp
├── core/                      # Core infrastructure
│   ├── spsc_queue.hpp         # Lock-free ring buffer
│   ├── raw_sample.hpp         # Sample data structure
│   └── fake_data.hpp          # Phase 1 fake data generator
├── state_machine.hpp          # Device state (IDLE/STREAMING)
├── command_handler.hpp        # Command processing
├── sampler.hpp                # Core 1 sampling logic (Phase 2)
├── powermonitor.cpp           # Main entry point
├── INA228.hpp/cpp             # Sensor driver (existing)
└── CMakeLists.txt
```

## Inter-Core Communication

RP2040 provides several mechanisms for communication between cores. This design uses:

1. **SPSC Queue** - For sample data flow (Core 1 → Core 0)
2. **Shared Memory** - For configuration and state flags
3. **Multicore FIFO** - For control commands (Core 0 → Core 1)


## Data Structures

### RawSample (internal queue format)

Compact representation of one sensor reading for the SPSC queue. The actual structure
has been extended beyond the original 20-byte design and now includes additional fields
(timestamp_unix_us, energy_raw, charge_raw, power_raw). See `device/core/raw_sample.hpp`
for the authoritative definition. The wire format `DataSamplePayload` is 41 bytes
(see `frame_defs.hpp`).

### Sample Ring Buffer

The project uses a custom, lock-free SPSC (Single-Producer, Single-Consumer) queue implemented in `device/core/spsc_queue.hpp`.

**Configuration:**
- Capacity: 512 samples
- Memory: ~10KB (512 × ~20 bytes per RawSample)
- At 1kHz sampling rate, this provides a ~512ms buffer.

## Inter-Core Communication

The design uses three mechanisms for inter-core communication:

1.  **SPSC Queue** (`core/spsc_queue.hpp`): For high-throughput, lock-free transfer of sample data from Core 1 (producer) to Core 0 (consumer).
2.  **Shared Memory** (`core/shared_context.hpp`): A `SharedContext` struct holds configuration (like `stream_period_us`) and status flags. Access is synchronized using memory barriers.
3.  **Multicore FIFO**: For sending simple commands (like `kStartStream`, `kStopStream`) from Core 0 to Core 1. A FIFO event also wakes Core 1 from its `__wfe()` state.

### Memory Barriers

To ensure Core 1 sees updated configuration from Core 0, memory barriers are critical. The compiler or CPU can reorder memory access, so we must enforce order.

```cpp
// Core 0: Before signaling Core 1
shared_ctx.stream_period_us = period_us;
__dmb(); // Data Memory Barrier ensures the write completes
multicore_fifo_push_blocking(kStartStream);

// Core 1: After receiving signal
uint32_t cmd = multicore_fifo_pop_blocking();
__dmb(); // Ensures we see the new value from shared_ctx
uint16_t period = shared_ctx.stream_period_us;
```

### Sequence Diagram

```mermaid
sequenceDiagram
    participant PC as PC (Host)
    participant C0 as Core 0 (USB/Protocol)
    participant Ctx as SharedContext
    participant FIFO as Multicore FIFO
    participant C1_Worker as Core 1 Worker Loop
    participant C1_ISR as Core 1 Timer ISR
    participant Q as Sample Queue

    Note over C0,C1_Worker: === STREAM_START ===

    PC->>C0: CMD: STREAM_START(period, mask)
    C0->>Ctx: stream_period_us = period
    Note over C0: __dmb() memory barrier
    C0->>FIFO: push(kStartStream)
    C0->>PC: RSP(OK)

    FIFO-->>C1_Worker: pop() -> kStartStream
    Note over C1_Worker: __dmb() memory barrier
    C1_Worker->>Ctx: read config
    C1_Worker->>C1_Worker: add_repeating_timer()

    loop Every period_us
        C1_ISR->>C1_ISR: Timer fires
        C1_ISR-->>C1_Worker: __sev()
        C1_Worker->>C1_Worker: Wakes from __wfe()
        C1_Worker->>C1_Worker: Read INA228 via I2C
        C1_Worker->>Q: push(sample)
    end

    loop Core 0 Main Loop
        Q-->>C0: pop(sample)
        C0->>PC: DATA: DATA_SAMPLE
    end
```

## Timing Considerations

### I2C Read Time

INA228 register read times (at 400kHz I2C):
- 16-bit register: ~50µs
- 24-bit register: ~75µs
- Full sample (5 registers): ~300µs

At a 1kHz sampling rate (1000µs period), the I2C reads in the worker loop consume ~30% of the time budget, leaving ample idle time.

---

*The rest of the document (Implementation Phases, File Structure, etc.) is largely conceptual and does not require updates.*
