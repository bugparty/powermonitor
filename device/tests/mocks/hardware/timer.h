#pragma once
#include <cstdint>

// Define repeating_timer types locally
typedef struct repeating_timer {
    int64_t delay_us;
    void *user_data;
} repeating_timer_t;

typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);

static bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out) {
    return true;
}

static bool cancel_repeating_timer(repeating_timer_t *timer) {
    return true;
}
