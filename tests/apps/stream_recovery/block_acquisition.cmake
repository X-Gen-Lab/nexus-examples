# Application C retains its production flags; only release is fault-injected.
add_library(
    block_acquisition_policy_test
    OBJECT
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery/adc_acquisition.c"
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery/audio_acquisition.c"
)
target_compile_definitions(
    block_acquisition_policy_test
    PRIVATE nx_stream_release=example_test_block_release
)
target_link_libraries(
    block_acquisition_policy_test
    PRIVATE Nexus::IO nexus_owned_warnings nexus_build_options
)
add_executable(
    block_acquisition_test
    "${CMAKE_CURRENT_LIST_DIR}/block_acquisition_test.cpp"
    "$<TARGET_OBJECTS:block_acquisition_policy_test>"
)
target_include_directories(
    block_acquisition_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/stream_recovery"
)
target_compile_features(block_acquisition_test PRIVATE cxx_std_17)
set_target_properties(block_acquisition_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    block_acquisition_test
    PRIVATE Nexus::IO GTest::gmock_main nexus_owned_warnings nexus_build_options
)
add_test(
    NAME block_acquisition_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:block_acquisition_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/block-acquisition-execution"
)
set_tests_properties(
    block_acquisition_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
