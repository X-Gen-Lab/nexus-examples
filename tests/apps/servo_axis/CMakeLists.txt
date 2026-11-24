# Public AxisControl and real scalar current PI close against an independent plant.
# Author: Nexus Team
include("${PROJECT_SOURCE_DIR}/recipes/control/servo_axis/firmware.cmake")
add_executable(servo_axis_test "${CMAKE_CURRENT_LIST_DIR}/servo_axis_test.cpp")
target_compile_features(servo_axis_test PRIVATE cxx_std_17)
set_target_properties(servo_axis_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    servo_axis_test
    PRIVATE
        example_servo_axis
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME servo_axis_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:servo_axis_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/servo-axis-google-execution"
)
set_tests_properties(
    servo_axis_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
