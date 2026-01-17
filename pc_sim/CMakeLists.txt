cmake_minimum_required(VERSION 3.13)

project(powermonitor_pc_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pc_sim
    ../app/main.cpp
    ../protocol/crc16_ccitt_false.cpp
    ../protocol/frame_builder.cpp
    ../protocol/parser.cpp
    ../protocol/unpack.cpp
    ../sim/event_loop.cpp
    ../sim/virtual_link.cpp
    ../node/ina228_model.cpp
    ../node/pc_node.cpp
    ../node/device_node.cpp
)

target_include_directories(pc_sim PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
