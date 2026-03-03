#pragma once
#include <cstdint>
#include <unistd.h>
#include <stdio.h>

// Mock time functions
static uint64_t mock_time_us = 0;
static uint64_t time_us_64() { return mock_time_us; }
static uint32_t time_us_32() { return (uint32_t)mock_time_us; }
static void sleep_ms(uint32_t ms) { mock_time_us += ms * 1000; }
static void sleep_us(uint64_t us) { mock_time_us += us; }

// Hardware functions
static void stdio_init_all() {}
static void busy_wait_us(uint64_t) {}

// Timer functions
struct repeating_timer_t {
    void* user_data;
};

typedef bool (*repeating_timer_callback_t)(repeating_timer_t* rt);

static bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void* user_data, repeating_timer_t* out) {
    if (out) out->user_data = user_data;
    return true;
}

static bool cancel_repeating_timer(repeating_timer_t* timer) {
    return true;
}
