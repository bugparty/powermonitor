cmake_minimum_required(VERSION 3.14)
project(asio_serial_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Asio standalone (不需要 Boost)
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

add_executable(serial_demo main.cpp)

target_include_directories(serial_demo PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(serial_demo PRIVATE ASIO_STANDALONE)

if(WIN32)
    target_link_libraries(serial_demo PRIVATE ws2_32 setupapi)
endif()
