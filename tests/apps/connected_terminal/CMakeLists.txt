# Test actual application, UI/route helpers and persistent Settings/Storage.
# Author: Nexus Team
include("${PROJECT_SOURCE_DIR}/apps/connected_terminal/application.cmake")
add_executable(connected_terminal_test terminal_application_test.cpp)
target_compile_features(connected_terminal_test PRIVATE cxx_std_17)
set_target_properties(connected_terminal_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    connected_terminal_test
    PRIVATE
        connected_terminal_application
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME connected_terminal_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:connected_terminal_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/google-execution"
)
set_tests_properties(
    connected_terminal_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
