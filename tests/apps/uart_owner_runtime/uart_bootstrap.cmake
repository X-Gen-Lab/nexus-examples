# Exercise the actual MCU entry and rollback with only external ports mocked.
if(EXISTS "${NEXUS_SOURCE}/.nexus-source-sdk.json")
    return()
endif()
set(uart_bootstrap_config "${CMAKE_CURRENT_BINARY_DIR}/uart-freertos")
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${NEXUS_SOURCE}/tools/configure/configure.py" --source-root
        "${NEXUS_SOURCE}" --assembly
        "${PROJECT_SOURCE_DIR}/configs/qiming-freertos.toml" --output
        "${uart_bootstrap_config}"
    RESULT_VARIABLE uart_bootstrap_result
    OUTPUT_VARIABLE uart_bootstrap_output
    ERROR_VARIABLE uart_bootstrap_error
)
if(NOT uart_bootstrap_result EQUAL 0)
    message(FATAL_ERROR "${uart_bootstrap_output}${uart_bootstrap_error}")
endif()
_nexus_watch_configuration_inputs(
    "${uart_bootstrap_config}" input_paths configuration_code_paths
)
add_library(
    uart_bootstrap_main
    OBJECT
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/main.c"
)
target_compile_definitions(
    uart_bootstrap_main
    PRIVATE main=example_uart_mcu_main
)
target_compile_options(
    uart_bootstrap_main
    PRIVATE "-include${CMAKE_CURRENT_LIST_DIR}/uart_bootstrap.h"
)
add_executable(
    uart_bootstrap_test
    "${CMAKE_CURRENT_LIST_DIR}/uart_bootstrap_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/lifecycle.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/bootstrap.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/scheduler_return.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/quarantine_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/fault_policy.c"
    $<TARGET_OBJECTS:uart_bootstrap_main>
)
foreach(target IN ITEMS uart_bootstrap_main uart_bootstrap_test)
    target_include_directories(
        ${target}
        BEFORE
        PRIVATE
            "${uart_bootstrap_config}"
            "${NEXUS_SOURCE}/os/freertos/include"
            "${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime"
            "${NEXUS_SOURCE}/ext/freertos/include"
            "${NEXUS_SOURCE}/ext/freertos/portable/ThirdParty/GCC/Posix"
            "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime"
    )
    target_link_libraries(
        ${target}
        PRIVATE
            Nexus::Platform
            Nexus::UARTOwner
            Nexus::OSWait
            nexus_owned_warnings
            nexus_build_options
    )
endforeach()
target_compile_features(uart_bootstrap_test PRIVATE cxx_std_17)
set_target_properties(uart_bootstrap_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(uart_bootstrap_test PRIVATE GTest::gmock_main)
foreach(
    symbol
    IN
    ITEMS
        nx_platform_start
        nx_platform_stop
        nx_os_run
        nx_notify_init
        nx_notify_port
        nx_notify_destroy
        nx_task_start
        nx_task_discard_before_scheduler
        nx_task_join_until
        nx_uart_port_attach_wake
        nx_uart_port_stop
        nx_fault_observe
        nx_freertos_assert_failed
)
    target_link_options(uart_bootstrap_test PRIVATE "LINKER:--wrap=${symbol}")
endforeach()
add_test(
    NAME uart_bootstrap_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:uart_bootstrap_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/uart-bootstrap-google-execution"
)
set_tests_properties(
    uart_bootstrap_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
