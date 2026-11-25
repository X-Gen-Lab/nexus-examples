# Execute the actual application and production ThreadX Linux adapter.
cmake_minimum_required(VERSION 3.31)
project(uart_owner_threadx_contract LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(
    examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_LIST_DIR}/assembly.toml")
include("${examples_root}/tests/cmake/GoogleTest.cmake")
examples_host_google_test()
set(application_root "${examples_root}/apps/uart_owner_runtime")
add_library(uart_owner_threadx_main OBJECT "${application_root}/main.c")
target_compile_definitions(
    uart_owner_threadx_main
    PRIVATE main=example_uart_threadx_main
)
target_compile_options(
    uart_owner_threadx_main
    PRIVATE "-include${CMAKE_CURRENT_LIST_DIR}/entry.h"
)
target_link_libraries(
    uart_owner_threadx_main
    PRIVATE
        Nexus::Platform
        Nexus::UARTOwner
        Nexus::OS
        Nexus::Arch
        Nexus::NativeModel
        nexus_owned_warnings
        nexus_build_options
)
add_executable(
    uart_owner_threadx_test
    threadx_application_test.cpp
    "${application_root}/lifecycle.c"
    "${application_root}/bootstrap.c"
    "${application_root}/scheduler_return.c"
    "${application_root}/quarantine_policy.c"
    "${application_root}/fault_policy.c"
    "${application_root}/fault_observer.c"
    $<TARGET_OBJECTS:uart_owner_threadx_main>
)
target_include_directories(
    uart_owner_threadx_test
    PRIVATE "${application_root}"
)
target_compile_features(uart_owner_threadx_test PRIVATE cxx_std_17)
set_target_properties(uart_owner_threadx_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    uart_owner_threadx_test
    PRIVATE
        Nexus::Platform
        Nexus::UARTOwner
        Nexus::OS
        Nexus::Arch
        Nexus::NativeModel
        GTest::gmock
        nexus_owned_warnings
        nexus_build_options
)
foreach(
    symbol
    nx_os_run
    nx_task_start
    nx_task_join_until
    nx_notify_destroy
    nx_platform_stop
    nx_uart_port_attach_wake
    nx_uart_port_stop
)
    target_link_options(
        uart_owner_threadx_test
        PRIVATE "LINKER:--wrap=${symbol}"
    )
endforeach()
add_executable(
    uart_owner_threadx_bootstrap_test
    "${CMAKE_CURRENT_LIST_DIR}/../uart_bootstrap_test.cpp"
    "${application_root}/lifecycle.c"
    "${application_root}/bootstrap.c"
    "${application_root}/scheduler_return.c"
    "${application_root}/quarantine_policy.c"
    "${application_root}/fault_policy.c"
    $<TARGET_OBJECTS:uart_owner_threadx_main>
)
target_include_directories(
    uart_owner_threadx_bootstrap_test
    PRIVATE "${application_root}"
)
target_compile_definitions(
    uart_owner_threadx_bootstrap_test
    PRIVATE example_uart_mcu_main=example_uart_threadx_main
)
target_compile_features(uart_owner_threadx_bootstrap_test PRIVATE cxx_std_17)
set_target_properties(
    uart_owner_threadx_bootstrap_test
    PROPERTIES CXX_EXTENSIONS OFF
)
target_link_libraries(
    uart_owner_threadx_bootstrap_test
    PRIVATE
        Nexus::Platform
        Nexus::UARTOwner
        Nexus::OS
        Nexus::Arch
        Nexus::NativeModel
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
foreach(
    symbol
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
    nx_threadx_fail_stop
)
    target_link_options(
        uart_owner_threadx_bootstrap_test
        PRIVATE "LINKER:--wrap=${symbol}"
    )
endforeach()
enable_testing()
add_test(
    NAME uart_owner_real_threadx
    COMMAND
        "${Python3_EXECUTABLE}" -B "${examples_root}/scripts/run_google.py"
        --nexus-source "${Nexus_SOURCE_DIR}" --binary
        "$<TARGET_FILE:uart_owner_threadx_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/runtime-google-execution"
)
set_tests_properties(
    uart_owner_real_threadx
    PROPERTIES TIMEOUT 15 LABELS "native;example;ThreadX;GoogleTest"
)
add_test(
    NAME uart_owner_threadx_bootstrap
    COMMAND
        "${Python3_EXECUTABLE}" -B "${examples_root}/scripts/run_google.py"
        --nexus-source "${Nexus_SOURCE_DIR}" --binary
        "$<TARGET_FILE:uart_owner_threadx_bootstrap_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-google-execution"
)
set_tests_properties(
    uart_owner_threadx_bootstrap
    PROPERTIES TIMEOUT 15 LABELS "native;example;ThreadX;GoogleTest"
)
