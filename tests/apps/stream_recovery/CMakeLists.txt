# Compile the production C policy; intercept only its public release boundary.
add_library(continuous_rx_policy_test OBJECT
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery/receive_application.c")
target_compile_definitions(continuous_rx_policy_test PRIVATE
    nx_stream_release=example_test_stream_release)
target_link_libraries(continuous_rx_policy_test PRIVATE Nexus::IO
    nexus_owned_warnings nexus_build_options)
add_executable(continuous_rx_test
    "${CMAKE_CURRENT_LIST_DIR}/continuous_rx_test.cpp"
    "$<TARGET_OBJECTS:continuous_rx_policy_test>")
target_include_directories(continuous_rx_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery")
target_compile_features(continuous_rx_test PRIVATE cxx_std_17)
set_target_properties(continuous_rx_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(continuous_rx_test PRIVATE Nexus::IO GTest::gmock_main
    nexus_owned_warnings nexus_build_options)
add_test(NAME continuous_rx_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:continuous_rx_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/continuous-rx-execution")
set_tests_properties(continuous_rx_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
