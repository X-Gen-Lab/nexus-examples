# Real C11 Native lifecycle is compiled directly; no main-text fixture.
add_executable(
    network_gateway_main_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_main_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/host.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_application.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/native_frames.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/native_runtime.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/host_peer.c"
)
target_include_directories(
    network_gateway_main_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/network_gateway"
)
target_compile_features(network_gateway_main_test PRIVATE cxx_std_17)
set_target_properties(network_gateway_main_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_gateway_main_test
    PRIVATE
        Nexus::LwIPStorage
        Nexus::LwIPTCPStorage
        Nexus::TLSStorage
        Nexus::CoreMQTTStorage
        Nexus::IOWatchdog
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
target_link_options(
    network_gateway_main_test
    PRIVATE
        "LINKER:--wrap=kill"
        "LINKER:--wrap=example_gateway_run"
        "LINKER:--wrap=nx_tls_runtime_start"
        "LINKER:--wrap=nx_lwip_adapter_stop"
)
add_test(
    NAME network_gateway_main_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_gateway_main_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/gateway-main-google-execution"
)
set_tests_properties(
    network_gateway_main_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
