#pragma once
#include <cstdint>

// Repeating timer structure
struct repeating_timer_t {
    int64_t delay_us;
    int64_t interval_us;
    void *user_data;
    bool alarm_id;
};

// Callback type
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);

// Mock implementation of add_repeating_timer_us
static bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out) {
    if (out) {
        out->delay_us = delay_us;
        out->interval_us = delay_us; // Assume interval same as delay for simplicity
        out->user_data = user_data;
        out->alarm_id = true; // Pretend we got an alarm ID
    }
    return true; // Success
}

// Mock implementation of cancel_repeating_timer
static bool cancel_repeating_timer(repeating_timer_t *timer) {
    if (timer) {
        timer->alarm_id = false;
    }
    return true;
}
