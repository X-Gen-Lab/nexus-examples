# Independent public API sensor/fusion composition.
# Author: Nexus Team
nexus_add_lsm6dsox()
add_executable(
    imu_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/imu_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/control/imu_reference.c"
)
target_include_directories(
    imu_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/control"
)
target_compile_features(imu_reference_test PRIVATE cxx_std_17)
set_target_properties(imu_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    imu_reference_test
    PRIVATE
        Nexus::LSM6DSOX
        Nexus::Algorithms
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME imu_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:imu_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/imu-reference-google-execution"
)
set_tests_properties(
    imu_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
