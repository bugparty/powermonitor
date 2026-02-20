project(powermonitor_pc_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common source files for the library
add_library(powermonitor_lib
        ../sim/event_loop.cpp
        ../sim/virtual_link.cpp
        ../node/ina228_model.cpp
        ../node/pc_node.cpp
        ../node/device_node.cpp
)
target_include_directories(powermonitor_lib PUBLIC .. )
target_link_libraries(powermonitor_lib PUBLIC powermonitor_protocol)

# Demo application
add_executable(pc_sim
        pc_sim_main.cpp
)
target_link_libraries(pc_sim powermonitor_lib)

# Time sync demo application
add_executable(pc_sim_time_sync_demo
        time_sync_demo.cpp
)
target_link_libraries(pc_sim_time_sync_demo powermonitor_lib)

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

# Test executable
add_executable(pc_sim_test
        functional_test.cpp
        state_machine_test.cpp
        ina228_model_test.cpp
        unpack_test.cpp
        event_loop_test.cpp
)
target_link_libraries(pc_sim_test
        powermonitor_lib
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(pc_sim_test)
