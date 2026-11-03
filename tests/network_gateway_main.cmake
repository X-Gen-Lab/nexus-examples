# Compile the actual C11 main in its maintained SDK context. The generated
# adapter exposes only lifecycle observations; application behavior stays C11.
set(gateway_main_fixture "${CMAKE_CURRENT_BINARY_DIR}/gateway_main_fixture.c")
file(WRITE "${gateway_main_fixture}" "
#define main gateway_fixture_entry
int gateway_fixture_entry(int argc, char** argv);
#include \"${PROJECT_SOURCE_DIR}/apps/network_gateway/main.c\"
#undef main
int gateway_fixture_join(pid_t child, int descriptor, int* observations);
int gateway_fixture_run(int argc, char** argv, int* observations);
int gateway_fixture_start_peer(int* observations);
int gateway_fixture_job(int selected);
int gateway_fixture_join(pid_t child, int descriptor, int* observations) {
    gateway_host_t state = {.peer = child, .control_descriptor = descriptor};
    const nx_result_t result = join_peer(&state);
    observations[0] = state.peer_joined;
    observations[1] = state.peer_report_valid;
    observations[2] = state.peer_report.local_resources_released;
    return (int)result;
}
int gateway_fixture_run(int argc, char** argv, int* observations) {
    const int result = gateway_fixture_entry(argc, argv);
    observations[0] = host.frame_descriptor;
    observations[1] = host.frames.descriptor;
    observations[2] = host.network_live;
    observations[3] = fcntl(host.frames.descriptor, F_GETFD) >= 0;
    return result;
}
int gateway_fixture_start_peer(int* observations) {
    gateway_host_t state = {.frame_descriptor = -1, .control_descriptor = -1};
    const nx_result_t started = start_peer(&state, 1U);
    if (started != NX_SUCCESS) {
        return (int)started;
    }
    struct sigaction action = {0};
    observations[0] = sigaction(SIGCHLD, NULL, &action) == 0;
    observations[1] = action.sa_handler == SIG_DFL;
    observations[2] = (action.sa_flags & SA_NOCLDWAIT) == 0;
    const nx_result_t result = join_peer(&state);
    observations[3] = state.peer_joined;
    observations[4] = state.peer_report_valid;
    observations[5] = state.peer_report.local_resources_released;
    (void)close(state.frame_descriptor);
    (void)close(state.control_descriptor);
    return (int)result;
}
int gateway_fixture_job(int selected) {
    gateway_host_t state = {.selected = (gateway_case_t)selected};
    return (int)run_job(&state);
}
")
add_executable(network_gateway_main_test network_gateway_main_test.cpp
    "${gateway_main_fixture}"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_application.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/gateway_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native_frames.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/native_runtime.c"
    "${PROJECT_SOURCE_DIR}/apps/network_gateway/host_peer.c")
target_include_directories(network_gateway_main_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/network_gateway")
target_compile_features(network_gateway_main_test PRIVATE cxx_std_17)
set_target_properties(network_gateway_main_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(network_gateway_main_test PRIVATE Nexus::LwIPStorage
    Nexus::LwIPTCPStorage Nexus::TLSStorage Nexus::CoreMQTTStorage
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
target_link_options(network_gateway_main_test PRIVATE "LINKER:--wrap=kill"
    "LINKER:--wrap=example_gateway_run"
    "LINKER:--wrap=nx_tls_runtime_start"
    "LINKER:--wrap=nx_lwip_adapter_stop")
add_test(NAME network_gateway_main_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:network_gateway_main_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/gateway-main-google-execution")
set_tests_properties(network_gateway_main_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
