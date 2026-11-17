# Real coreMQTT parses protocol bytes; TLS/TCP are external dependency mocks.
# Native enables C++ only for this contract, never for the application firmware.
nexus_add_coremqtt()
nexus_add_tls()
add_executable(
    network_gateway_application_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_application_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_application.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_policy.c"
)
target_include_directories(
    network_gateway_application_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/network_gateway"
)
target_compile_features(network_gateway_application_test PRIVATE cxx_std_17)
set_target_properties(
    network_gateway_application_test
    PROPERTIES CXX_EXTENSIONS OFF
)
target_link_libraries(
    network_gateway_application_test
    PRIVATE
        Nexus::LwIPTCPStorage
        Nexus::TLSStorage
        Nexus::CoreMQTTStorage
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
target_link_options(
    network_gateway_application_test
    PRIVATE "LINKER:--wrap=nx_mqtt_deinit"
)
add_test(
    NAME network_gateway_application_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_gateway_application_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/gateway-application-google-execution"
)
set_tests_properties(
    network_gateway_application_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
