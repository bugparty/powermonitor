#pragma once
#include <cstdint>

typedef struct repeating_timer repeating_timer_t;
struct repeating_timer {
    int64_t delay_us;
    void *user_data;
};

// Mock function declaration (no implementation in header to avoid multiple definition)
// But since this is a header-only mock strategy seen in other files, we might need inline or static.
// Looking at stdlib.h, it uses static functions.

static bool add_repeating_timer_us(int64_t delay_us, bool (*callback)(repeating_timer_t *), void *user_data, repeating_timer_t *out) {
    out->delay_us = delay_us;
    out->user_data = user_data;
    return true;
}

static bool cancel_repeating_timer(repeating_timer_t *timer) {
    return true;
}
