# The product retry policy is C11; GoogleTest remains host-only. The complete
# network composition is a separate consumer of the same application policy.
add_executable(
    network_gateway_policy_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_policy_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_policy.c"
)
target_include_directories(
    network_gateway_policy_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/network_gateway"
)
target_compile_features(network_gateway_policy_test PRIVATE cxx_std_17)
set_target_properties(network_gateway_policy_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_gateway_policy_test
    PRIVATE
        Nexus::Core
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME network_gateway_policy_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_gateway_policy_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/gateway-policy-google-execution"
)
set_tests_properties(
    network_gateway_policy_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

# This host-only frame substrate borrows an explicit UNIX datagram socket.
add_executable(
    network_gateway_frames_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_frames_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/native_frames.c"
)
target_include_directories(
    network_gateway_frames_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/network_gateway"
)
target_compile_features(network_gateway_frames_test PRIVATE cxx_std_17)
set_target_properties(network_gateway_frames_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_gateway_frames_test
    PRIVATE Nexus::IO GTest::gmock_main nexus_owned_warnings nexus_build_options
)
add_test(
    NAME network_gateway_frames_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_gateway_frames_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/gateway-frames-google-execution"
)
set_tests_properties(
    network_gateway_frames_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

include("${CMAKE_CURRENT_LIST_DIR}/network_gateway_application.cmake")
