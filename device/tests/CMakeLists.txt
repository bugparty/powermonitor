cmake_minimum_required(VERSION 3.13)
project(device_tests)

set(CMAKE_CXX_STANDARD 17)

# Test executable for CommandHandler
add_executable(test_command_handler test_command_handler.cpp)
target_include_directories(test_command_handler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/ina228_mock
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

# Test executable for INA228
add_executable(test_ina228 test_ina228.cpp ../INA228.cpp)
target_include_directories(test_ina228 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

# Test executable for Sampler
add_executable(test_sampler test_sampler.cpp)
target_include_directories(test_sampler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/ina228_mock
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)
