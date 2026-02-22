#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct repeating_timer repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
struct repeating_timer {
    int64_t delay_us;
    repeating_timer_callback_t callback;
    void *user_data;
};
bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out);
bool cancel_repeating_timer(repeating_timer_t *timer);
#ifdef __cplusplus
}
#endif
