cmake_minimum_required(VERSION 3.13)
project(device_tests)

set(CMAKE_CXX_STANDARD 17)

# Include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

# Test executable
add_executable(test_command_handler test_command_handler.cpp)

# Test executable for INA228
add_executable(test_ina228 test_ina228.cpp ../INA228.cpp)

# Test executable for Sampler Decimation
add_executable(test_sampler_decimation test_sampler_decimation.cpp ../INA228.cpp)

# No libraries needed as everything is header-only or mocked inline
