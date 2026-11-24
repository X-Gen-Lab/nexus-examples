# Real C mechanical sequencing over the public Axis component.
# Author: Nexus Team
include("${PROJECT_SOURCE_DIR}/recipes/control/lift_axis/firmware.cmake")
add_executable(lift_axis_test "${CMAKE_CURRENT_LIST_DIR}/lift_axis_test.cpp")
target_compile_features(lift_axis_test PRIVATE cxx_std_17)
set_target_properties(lift_axis_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    lift_axis_test
    PRIVATE
        example_lift_model
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME lift_axis_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:lift_axis_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/lift-axis-google-execution"
)
set_tests_properties(
    lift_axis_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
