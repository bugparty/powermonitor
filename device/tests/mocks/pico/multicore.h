#pragma once
#include <cstdint>

// Typedef for core 1 entry function
typedef void (*core1_entry_func_t)(void);

static bool mock_fifo_rvalid = false;
static uint32_t mock_fifo_pop_val = 0;
static uint32_t mock_fifo_push_val = 0;
static core1_entry_func_t mock_launch_core1_func = nullptr;

static inline bool multicore_fifo_rvalid() {
    return mock_fifo_rvalid;
}

static inline uint32_t multicore_fifo_pop_blocking() {
    return mock_fifo_pop_val;
}

static inline void multicore_launch_core1(core1_entry_func_t entry) {
    mock_launch_core1_func = entry;
}

static inline void multicore_fifo_push_blocking(uint32_t data) {
    mock_fifo_push_val = data;
}

// Reset mock state
static inline void multicore_mock_reset() {
    mock_fifo_rvalid = false;
    mock_fifo_pop_val = 0;
    mock_fifo_push_val = 0;
    mock_launch_core1_func = nullptr;
}
