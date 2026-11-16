# Two real NO_SYS stacks and an independent loopback TCP server exercise the
# opaque ciphertext relay, including broker send-only shutdown.
add_executable(network_gateway_peer_test
    "${CMAKE_CURRENT_LIST_DIR}/network_gateway_peer_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/host_peer.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/native_frames.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native/native_runtime.c")
target_include_directories(network_gateway_peer_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/network_gateway")
target_compile_features(network_gateway_peer_test PRIVATE cxx_std_17)
set_target_properties(network_gateway_peer_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(network_gateway_peer_test PRIVATE Nexus::LwIPStorage
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
target_link_options(network_gateway_peer_test PRIVATE
    "LINKER:--wrap=clock_gettime" "LINKER:--wrap=connect" "LINKER:--wrap=send"
    "LINKER:--wrap=recv" "LINKER:--wrap=tcp_close" "LINKER:--wrap=shutdown")
add_test(NAME network_gateway_peer_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:network_gateway_peer_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/gateway-peer-google-execution")
set_tests_properties(network_gateway_peer_google PROPERTIES TIMEOUT 60
    LABELS "native;example;GoogleTest")
