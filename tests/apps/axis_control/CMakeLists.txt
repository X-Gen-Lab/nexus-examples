# Real application C11; only hardware operations use host fixtures.
# Author: Nexus Team
add_library(
    example_axis_control
    STATIC
    "${PROJECT_SOURCE_DIR}/apps/axis_control/axis_control.c"
    "${PROJECT_SOURCE_DIR}/apps/axis_control/axis_parameters.c"
)
target_include_directories(
    example_axis_control
    PUBLIC "${PROJECT_SOURCE_DIR}/apps/axis_control"
)
target_link_libraries(
    example_axis_control
    PUBLIC Nexus::Algorithms Nexus::IOEncoder Nexus::IOPWMGroup Nexus::Settings
    PRIVATE nexus_owned_warnings nexus_build_options
)
add_executable(
    axis_control_test
    "${CMAKE_CURRENT_LIST_DIR}/axis_control_test.cpp"
)
target_compile_features(axis_control_test PRIVATE cxx_std_17)
set_target_properties(axis_control_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    axis_control_test
    PRIVATE
        example_axis_control
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME axis_control_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:axis_control_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/axis-control-google-execution"
)
set_tests_properties(
    axis_control_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
