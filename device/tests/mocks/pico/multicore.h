#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*multicore_entry_t)(void);
void multicore_launch_core1(multicore_entry_t entry);
void multicore_fifo_push_blocking(uint32_t data);
uint32_t multicore_fifo_pop_blocking();
bool multicore_fifo_rvalid();
#ifdef __cplusplus
}
#endif
