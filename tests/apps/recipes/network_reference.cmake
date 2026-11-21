# Product-owned upgrade and exclusive USB/local media policy contracts.
# Author: Nexus Team
add_executable(
    network_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/network_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/network/update_reference.c"
    "${PROJECT_SOURCE_DIR}/recipes/network/msc_handoff.c"
)
target_include_directories(
    network_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/network"
)
target_compile_features(network_reference_test PRIVATE cxx_std_17)
set_target_properties(network_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    network_reference_test
    PRIVATE
        Nexus::IOFlash
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME network_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:network_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/network-reference-google-execution"
)
set_tests_properties(
    network_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
