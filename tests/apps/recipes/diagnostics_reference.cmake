# Caller-owned diagnostics storage, finite execution and ordered withdrawal.
# Author: Nexus Team
nexus_add_shell()
add_executable(
    diagnostics_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/diagnostics/maintenance.c"
)
target_include_directories(
    diagnostics_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/diagnostics"
)
target_compile_features(diagnostics_reference_test PRIVATE cxx_std_17)
set_target_properties(diagnostics_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    diagnostics_reference_test
    PRIVATE
        Nexus::ShellStorage
        Nexus::Shell
        Nexus::Health
        Nexus::Fault
        Nexus::Log
        Nexus::NativeModel
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME diagnostics_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:diagnostics_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/diagnostics-reference-google-execution"
)
set_tests_properties(
    diagnostics_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
