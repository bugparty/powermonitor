#pragma once
#include <cstdint>

enum class FifoCmd {
    kStartStream,
    kStopStream
};

static inline void multicore_launch_core1(void (*entry)(void)) {
    (void)entry;
}

static inline void multicore_fifo_push_blocking(uint32_t data) {
    (void)data;
}

static inline bool multicore_fifo_rvalid() {
    return false;
}

static inline uint32_t multicore_fifo_pop_blocking() {
    return 0;
}
