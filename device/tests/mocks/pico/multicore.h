#pragma once
#include <cstdint>

typedef void (*core_func_t)(void);

static void multicore_launch_core1(core_func_t entry) {}
static bool multicore_fifo_rvalid() { return false; }
static uint32_t multicore_fifo_pop_blocking() { return 0; }
static void multicore_fifo_push_blocking(uint32_t data) {}
