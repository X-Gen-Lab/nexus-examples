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
        example_as5047p_rotor
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
include("${PROJECT_SOURCE_DIR}/recipes/control/dual_motor_drive/firmware.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/rotor.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/bus.cmake")
add_executable(pmsm_plant_test "${CMAKE_CURRENT_LIST_DIR}/pmsm_plant_test.cpp")
target_compile_features(pmsm_plant_test PRIVATE cxx_std_17)
set_target_properties(pmsm_plant_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    pmsm_plant_test
    PRIVATE
        example_pmsm_plant
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME pmsm_plant_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:pmsm_plant_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/pmsm-plant-google-execution"
)
set_tests_properties(
    pmsm_plant_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
add_executable(dual_motor_test "${CMAKE_CURRENT_LIST_DIR}/dual_motor_test.cpp")
target_compile_features(dual_motor_test PRIVATE cxx_std_17)
set_target_properties(dual_motor_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    dual_motor_test
    PRIVATE
        example_dual_motor_drive
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME dual_motor_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:dual_motor_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/dual-motor-google-execution"
)
set_tests_properties(
    dual_motor_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
include("${CMAKE_CURRENT_LIST_DIR}/cia402_motor.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/single_shunt.cmake")
