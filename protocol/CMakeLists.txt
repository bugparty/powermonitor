project(powermonitor_protocol LANGUAGES CXX)

add_library(powermonitor_protocol
    crc16_ccitt_false.cpp
    frame_builder.cpp
    parser.cpp
    unpack.cpp
)

target_include_directories(powermonitor_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(powermonitor_protocol PUBLIC cxx_std_17)
