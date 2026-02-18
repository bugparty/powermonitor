#pragma once
#include <cstdint>

typedef struct repeating_timer {
    void* user_data;
} repeating_timer_t;

static inline bool add_repeating_timer_us(int64_t delay_us, bool (*callback)(repeating_timer_t *rt), void *user_data, repeating_timer_t *out) {
    (void)delay_us;
    (void)callback;
    (void)user_data;
    (void)out;
    return true;
}

static inline bool cancel_repeating_timer(repeating_timer_t *rt) {
    (void)rt;
    return true;
}
