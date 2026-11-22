# Actual route composition calls public services; dependencies are test ports.
# Author: Nexus Team
nexus_add_lwip()
add_executable(
    network_route_test
    "${CMAKE_CURRENT_LIST_DIR}/network_route_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/network/route_reference.c"
)
target_include_directories(
    network_route_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/network"
)
target_compile_features(network_route_test PRIVATE cxx_std_17)
set_target_properties(network_route_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_route_test
    PRIVATE
        Nexus::LwIPManagedServices
        Nexus::WallClock
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME network_route_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_route_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/network-route-google-execution"
)
set_tests_properties(
    network_route_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
