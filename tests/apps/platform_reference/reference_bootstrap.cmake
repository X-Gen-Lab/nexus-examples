# Compile the authored MCU selection and the real application entry point.
# Only external OS/bootstrap ports are wrapped; no handwritten config header.
if(EXISTS "${NEXUS_SOURCE}/.nexus-source-sdk.json")
    # The maintained POSIX kernel layout fixture is development source only.
    return()
endif()
set(reference_bootstrap_config "${CMAKE_CURRENT_BINARY_DIR}/reference-freertos")
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${NEXUS_SOURCE}/tools/configure/configure.py" --source-root
        "${NEXUS_SOURCE}" --assembly
        "${PROJECT_SOURCE_DIR}/configs/qiming-freertos.toml" --output
        "${reference_bootstrap_config}"
    RESULT_VARIABLE reference_bootstrap_result
    OUTPUT_VARIABLE reference_bootstrap_output
    ERROR_VARIABLE reference_bootstrap_error
)
if(NOT reference_bootstrap_result EQUAL 0)
    message(
        FATAL_ERROR
        "${reference_bootstrap_output}${reference_bootstrap_error}"
    )
endif()
_nexus_watch_configuration_inputs(
    "${reference_bootstrap_config}" input_paths configuration_code_paths
)

add_library(
    reference_bootstrap_main
    OBJECT
    "${PROJECT_SOURCE_DIR}/apps/platform_reference/main.c"
)
target_compile_definitions(
    reference_bootstrap_main
    PRIVATE main=example_reference_main
)
target_compile_options(
    reference_bootstrap_main
    PRIVATE "-include${CMAKE_CURRENT_LIST_DIR}/reference_bootstrap.h"
)
add_executable(
    reference_bootstrap_test
    "${CMAKE_CURRENT_LIST_DIR}/reference_bootstrap_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/platform_reference/rx_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/platform_reference/finite_uart.c"
    $<TARGET_OBJECTS:reference_bootstrap_main>
)
foreach(target IN ITEMS reference_bootstrap_main reference_bootstrap_test)
    target_include_directories(
        ${target}
        BEFORE
        PRIVATE
            "${reference_bootstrap_config}"
            "${NEXUS_SOURCE}/os/freertos/include"
            "${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime"
            "${NEXUS_SOURCE}/ext/freertos/include"
            "${NEXUS_SOURCE}/ext/freertos/portable/ThirdParty/GCC/Posix"
            "${PROJECT_SOURCE_DIR}/apps/platform_reference"
    )
    target_link_libraries(
        ${target}
        PRIVATE
            Nexus::Platform
            Nexus::OSWait
            nexus_owned_warnings
            nexus_build_options
    )
endforeach()
target_compile_features(reference_bootstrap_test PRIVATE cxx_std_17)
set_target_properties(reference_bootstrap_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(reference_bootstrap_test PRIVATE GTest::gmock_main)
foreach(
    symbol
    IN
    ITEMS
        nx_platform_start_diagnosed
        nx_platform_stop_diagnosed
        nx_os_run
        nx_task_start
        nx_os_now_us
        nx_sleep_until
)
    target_link_options(
        reference_bootstrap_test
        PRIVATE "LINKER:--wrap=${symbol}"
    )
endforeach()
add_test(
    NAME reference_bootstrap_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:reference_bootstrap_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/reference-bootstrap-google-execution"
)
set_tests_properties(
    reference_bootstrap_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
