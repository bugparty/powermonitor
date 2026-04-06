cmake_minimum_required(VERSION 3.16)
project(powermonitor_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 89ff142b991af432b5d7a7cee55282f082a7e629
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v6.1.9
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cli11 nlohmann_json yaml-cpp asio googletest ftxui)

add_library(serial
    src/serial/serial.cpp
    include/serial/serial.h
)

target_include_directories(serial PRIVATE
    ${asio_SOURCE_DIR}/asio/include
)

target_include_directories(serial PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(serial PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(serial PRIVATE ws2_32 setupapi)
elseif (UNIX)
    target_link_libraries(serial PRIVATE pthread)
endif()

add_library(pc_client_lib
    src/sample_queue.cpp
    src/response_queue.cpp
    src/read_thread.cpp
    src/session.cpp
    src/thread_affinity.cpp
    src/onboard_sampler.cpp
    src/onboard_sample_queue.cpp
)

target_link_libraries(pc_client_lib PUBLIC
    powermonitor_protocol
    serial
    nlohmann_json::nlohmann_json
)

target_compile_definitions(pc_client_lib PRIVATE ASIO_STANDALONE)

target_include_directories(pc_client_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

add_executable(host_pc_client
    src/main.cpp
    src/power_monitor_session.cpp
)

target_link_libraries(host_pc_client
    pc_client_lib
    CLI11::CLI11
    yaml-cpp
    ftxui::screen
    ftxui::dom
    ftxui::component
)

target_compile_definitions(host_pc_client PRIVATE ASIO_STANDALONE)

target_include_directories(host_pc_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

add_executable(pc_time_sync_demo
    src/time_sync_demo.cpp
)

target_link_libraries(pc_time_sync_demo
    pc_client_lib
)

target_compile_definitions(pc_time_sync_demo PRIVATE ASIO_STANDALONE)

target_include_directories(pc_time_sync_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

add_executable(pc_client_tests
    tests/device_serial_tests.cpp
)

target_link_libraries(pc_client_tests PRIVATE
    pc_client_lib
    gtest_main
)

if(NOT MSVC AND UNIX)
    target_link_libraries(pc_client_tests PRIVATE pthread)
endif()

target_include_directories(pc_client_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

target_compile_definitions(pc_client_tests PRIVATE ASIO_STANDALONE)

enable_testing()
include(GoogleTest)
gtest_discover_tests(pc_client_tests)

if(MSVC)
  target_compile_options(host_pc_client PUBLIC "/ZI")
  target_link_options(host_pc_client PUBLIC "/INCREMENTAL")
  target_compile_options(pc_time_sync_demo PUBLIC "/ZI")
  target_link_options(pc_time_sync_demo PUBLIC "/INCREMENTAL")
  target_compile_options(pc_client_tests PUBLIC "/ZI")
  target_link_options(pc_client_tests PUBLIC "/INCREMENTAL")
endif()

# CPU Affinity Benchmark (Linux-only)
if(NOT MSVC AND UNIX)
    add_executable(cpu_affinity_benchmark
        ${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/cpu_affinity_benchmark.cpp
        src/thread_affinity.cpp
    )

    target_include_directories(cpu_affinity_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(cpu_affinity_benchmark PRIVATE pthread rt)

    target_compile_options(cpu_affinity_benchmark PRIVATE -O2)
endif()
