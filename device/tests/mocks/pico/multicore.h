#pragma once
#include <cstdint>

static void multicore_launch_core1(void (*entry)(void)) {}
static void multicore_fifo_push_blocking(uint32_t data) {}
static uint32_t multicore_fifo_pop_blocking() { return 0; }
static bool multicore_fifo_rvalid() { return false; }
static void __sev() {}
static void __wfe() {}
