#pragma once
#include <cstdint>
#include <queue>

// Mock multicore functions

static std::queue<uint32_t> fifo_core0_to_core1;
static std::queue<uint32_t> fifo_core1_to_core0;

static void multicore_launch_core1(void (*entry)(void)) {
    // In test, we might call this manually or ignore
}

static void multicore_fifo_push_blocking(uint32_t data) {
    fifo_core0_to_core1.push(data);
}

static uint32_t multicore_fifo_pop_blocking() {
    if (fifo_core0_to_core1.empty()) return 0;
    uint32_t val = fifo_core0_to_core1.front();
    fifo_core0_to_core1.pop();
    return val;
}

static bool multicore_fifo_rvalid() {
    return !fifo_core0_to_core1.empty();
}

static void multicore_fifo_drain() {
    while (!fifo_core0_to_core1.empty()) fifo_core0_to_core1.pop();
    while (!fifo_core1_to_core0.empty()) fifo_core1_to_core0.pop();
}
