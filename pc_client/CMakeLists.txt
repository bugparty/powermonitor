project(powermonitor_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    serial
    GIT_REPOSITORY https://github.com/wjwwood/serial.git
    GIT_TAG 1.2.1
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
    GIT_TAG 0.8.0
)

FetchContent_MakeAvailable(cli11 nlohmann_json yaml-cpp)

FetchContent_GetProperties(serial)
if (NOT serial_POPULATED)
    FetchContent_Populate(serial)
endif()

set(serial_sources
    ${serial_SOURCE_DIR}/src/serial.cc
    ${serial_SOURCE_DIR}/include/serial/serial.h
    ${serial_SOURCE_DIR}/include/serial/v8stdint.h
)

if (APPLE)
    list(APPEND serial_sources
        ${serial_SOURCE_DIR}/src/impl/unix.cc
        ${serial_SOURCE_DIR}/src/impl/list_ports/list_ports_osx.cc
    )
elseif (UNIX)
    list(APPEND serial_sources
        ${serial_SOURCE_DIR}/src/impl/unix.cc
        ${serial_SOURCE_DIR}/src/impl/list_ports/list_ports_linux.cc
    )
else()
    list(APPEND serial_sources
        ${serial_SOURCE_DIR}/src/impl/win.cc
        ${serial_SOURCE_DIR}/src/impl/list_ports/list_ports_win.cc
    )
endif()

add_library(serial ${serial_sources})
target_include_directories(serial PUBLIC ${serial_SOURCE_DIR}/include)

if (APPLE)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(FOUNDATION_LIBRARY Foundation)
    target_link_libraries(serial PRIVATE ${FOUNDATION_LIBRARY} ${IOKIT_LIBRARY})
elseif (UNIX)
    target_link_libraries(serial PRIVATE rt pthread)
else()
    target_link_libraries(serial PRIVATE setupapi)
endif()

add_executable(powermonitor
    src/main.cpp
)

target_link_libraries(powermonitor
    powermonitor_protocol
    serial
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    yaml-cpp
)

target_include_directories(powermonitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)
