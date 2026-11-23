# Product-owned durable delivery and bounded command admission.
# Author: Nexus Team
nexus_add_flashdb()
add_executable(
    network_delivery_test
    "${CMAKE_CURRENT_LIST_DIR}/network_delivery_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/network/delivery_reference.c"
    "${PROJECT_SOURCE_DIR}/recipes/network/boot_audit_reference.c"
)
target_include_directories(
    network_delivery_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/network"
)
target_compile_features(network_delivery_test PRIVATE cxx_std_17)
set_target_properties(network_delivery_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_delivery_test
    PRIVATE
        Nexus::FlashDB
        Nexus::Algorithms
        Nexus::NativeModel
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME network_delivery_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_delivery_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/network-delivery-google-execution"
)
set_tests_properties(
    network_delivery_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
