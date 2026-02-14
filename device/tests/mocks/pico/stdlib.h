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
