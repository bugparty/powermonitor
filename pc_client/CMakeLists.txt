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

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cli11 nlohmann_json yaml-cpp asio googletest)

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

add_executable(powermonitor
    src/main.cpp
    src/power_monitor_session.cpp
)

target_link_libraries(powermonitor
    pc_client_lib
    CLI11::CLI11
    yaml-cpp
)

target_compile_definitions(powermonitor PRIVATE ASIO_STANDALONE)

target_include_directories(powermonitor PRIVATE
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
  target_compile_options(powermonitor PUBLIC "/ZI")
  target_link_options(powermonitor PUBLIC "/INCREMENTAL")
  target_compile_options(pc_client_tests PUBLIC "/ZI")
  target_link_options(pc_client_tests PUBLIC "/INCREMENTAL")
endif()
