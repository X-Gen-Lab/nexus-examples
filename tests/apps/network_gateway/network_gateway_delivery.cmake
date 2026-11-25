# The delivery seam preserves the existing gateway's transport implementation.
# Author: Nexus Team
nexus_add_flashdb()
nexus_add_lwip()
nexus_add_coremqtt()
nexus_add_tls()
add_executable(
    network_gateway_delivery_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_delivery_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_delivery.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_managed.c"
    "${PROJECT_SOURCE_DIR}/recipes/network/delivery_reference.c"
    "${PROJECT_SOURCE_DIR}/recipes/network/route_reference.c"
)
target_include_directories(
    network_gateway_delivery_test
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/network_gateway"
        "${PROJECT_SOURCE_DIR}/recipes/network"
)
target_compile_features(network_gateway_delivery_test PRIVATE cxx_std_17)
set_target_properties(
    network_gateway_delivery_test
    PROPERTIES CXX_EXTENSIONS OFF
)
target_link_libraries(
    network_gateway_delivery_test
    PRIVATE
        Nexus::FlashDB
        Nexus::Algorithms
        Nexus::LwIPManagedTCP
        Nexus::LwIPManagedServices
        Nexus::WallClock
        Nexus::TLS
        Nexus::CoreMQTT
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME network_gateway_delivery_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_gateway_delivery_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/gateway-delivery-google-execution"
)
set_tests_properties(
    network_gateway_delivery_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
