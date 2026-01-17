project(powermonitor_pc_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Common source files for the library
add_library(powermonitor_lib
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
target_include_directories(powermonitor_lib PUBLIC .. )

# Test executable
add_executable(pc_sim_test
        main.cpp
)
target_link_libraries(pc_sim_test
        powermonitor_lib
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(pc_sim_test)
