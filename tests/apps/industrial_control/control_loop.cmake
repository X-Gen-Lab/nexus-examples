# Real public stream and algorithms; mock only external ADC/PWM operations.
# Author: Nexus Team
add_executable(
    industrial_control_loop_test
    "${CMAKE_CURRENT_LIST_DIR}/control_loop_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/control_loop.c"
)
target_include_directories(
    industrial_control_loop_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/industrial_control"
)
target_compile_features(industrial_control_loop_test PRIVATE cxx_std_17)
set_target_properties(
    industrial_control_loop_test
    PROPERTIES CXX_EXTENSIONS OFF
)
target_link_libraries(
    industrial_control_loop_test
    PRIVATE
        Nexus::Algorithms
        Nexus::FSM
        Nexus::IO
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME industrial_control_loop_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:industrial_control_loop_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/control-loop-google-execution"
)
set_tests_properties(
    industrial_control_loop_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
