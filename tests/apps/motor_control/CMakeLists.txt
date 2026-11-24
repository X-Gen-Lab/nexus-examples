# Closed-loop examples retain actual C and independent host plant models.
# Author: Nexus Team
include(
    "${PROJECT_SOURCE_DIR}/recipes/control/differential_drive/firmware.cmake"
)
add_executable(
    differential_drive_test
    "${CMAKE_CURRENT_LIST_DIR}/differential_drive_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/drive_control_test.cpp"
)
target_compile_features(differential_drive_test PRIVATE cxx_std_17)
set_target_properties(differential_drive_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    differential_drive_test
    PRIVATE
        example_differential_drive
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME differential_drive_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:differential_drive_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/differential-drive-google-execution"
)
set_tests_properties(
    differential_drive_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
include("${PROJECT_SOURCE_DIR}/recipes/control/motor_drive/firmware.cmake")
add_executable(
    motor_drive_test
    "${CMAKE_CURRENT_LIST_DIR}/motor_drive_test.cpp"
)
target_compile_features(motor_drive_test PRIVATE cxx_std_17)
set_target_properties(motor_drive_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    motor_drive_test
    PRIVATE
        example_motor_drive
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME motor_drive_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:motor_drive_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/motor-drive-google-execution"
)
set_tests_properties(
    motor_drive_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
