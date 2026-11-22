# Bounded drawing, real calibrated touch and durable raw fault export.
# Author: Nexus Team
add_executable(
    terminal_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/terminal_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/ui/display_reference.c"
    "${PROJECT_SOURCE_DIR}/recipes/control/touch_reference.c"
    "${PROJECT_SOURCE_DIR}/recipes/diagnostics/fault_export.c"
)
target_include_directories(
    terminal_reference_test
    PRIVATE
        "${PROJECT_SOURCE_DIR}/recipes/ui"
        "${PROJECT_SOURCE_DIR}/recipes/diagnostics"
)
target_compile_features(terminal_reference_test PRIVATE cxx_std_17)
set_target_properties(terminal_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    terminal_reference_test
    PRIVATE
        Nexus::ILI9341
        Nexus::XPT2046
        Nexus::FaultStorage
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME terminal_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:terminal_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/terminal-reference-google-execution"
)
set_tests_properties(
    terminal_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

# Host mocks only the external Platform/System call boundary for this recipe.
add_executable(
    power_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/power_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/power/baremetal_stop_reference.c"
)
target_include_directories(
    power_reference_test
    PRIVATE
        "${PROJECT_SOURCE_DIR}/recipes/power"
        "${NEXUS_SOURCE}/soc/stm32/stm32f4/include"
)
target_compile_features(power_reference_test PRIVATE cxx_std_17)
set_target_properties(power_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    power_reference_test
    PRIVATE
        Nexus::Core
        Nexus::ClockDomain
        Nexus::IORTC
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME power_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:power_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/power-reference-google-execution"
)
set_tests_properties(
    power_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
