#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i2c_inst i2c_inst_t;

// Mock declarations
int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint64_t timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint64_t timeout_us);

#ifdef __cplusplus
}
#endif
