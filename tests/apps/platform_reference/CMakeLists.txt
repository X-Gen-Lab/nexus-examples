# Compile the maintained application sources with their host contracts.
add_executable(
    reference_rx_policy_test
    "${CMAKE_CURRENT_LIST_DIR}/reference_rx_policy_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/platform_reference/rx_policy.c"
)
target_include_directories(
    reference_rx_policy_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/platform_reference"
)
target_compile_features(reference_rx_policy_test PRIVATE cxx_std_17)
set_target_properties(reference_rx_policy_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    reference_rx_policy_test
    PRIVATE Nexus::IO GTest::gmock_main nexus_owned_warnings nexus_build_options
)
add_test(
    NAME reference_rx_policy_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:reference_rx_policy_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/reference-google-execution"
)
set_tests_properties(
    reference_rx_policy_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

add_executable(
    reference_lifecycle_test
    "${CMAKE_CURRENT_LIST_DIR}/reference_lifecycle_test.cpp"
)
target_compile_features(reference_lifecycle_test PRIVATE cxx_std_17)
set_target_properties(reference_lifecycle_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    reference_lifecycle_test
    PRIVATE
        Nexus::Platform
        Nexus::NativeModel
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME reference_lifecycle_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:reference_lifecycle_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/lifecycle-google-execution"
)
set_tests_properties(
    reference_lifecycle_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
