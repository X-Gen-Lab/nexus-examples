# The real MCU composition uses authored facts and selected OS storage layouts.
if(EXISTS "${NEXUS_SOURCE}/.nexus-source-sdk.json")
    return()
endif()
set(spi_bootstrap_config "${CMAKE_CURRENT_BINARY_DIR}/spi-freertos")
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${NEXUS_SOURCE}/tools/configure/configure.py" --source-root
        "${NEXUS_SOURCE}" --assembly
        "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/assembly.toml" --output
        "${spi_bootstrap_config}"
    RESULT_VARIABLE spi_bootstrap_result
    OUTPUT_VARIABLE spi_bootstrap_output
    ERROR_VARIABLE spi_bootstrap_error
)
if(NOT spi_bootstrap_result EQUAL 0)
    message(FATAL_ERROR "${spi_bootstrap_output}${spi_bootstrap_error}")
endif()
_nexus_watch_configuration_inputs(
    "${spi_bootstrap_config}" input_paths configuration_code_paths
)
add_library(
    spi_bootstrap_application
    OBJECT
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/shared_spi_application.c"
)
add_executable(
    spi_bootstrap_test
    "${CMAKE_CURRENT_LIST_DIR}/spi_bootstrap_test.cpp"
    $<TARGET_OBJECTS:spi_bootstrap_application>
)
foreach(target IN ITEMS spi_bootstrap_application spi_bootstrap_test)
    target_include_directories(
        ${target}
        BEFORE
        PRIVATE
            "${spi_bootstrap_config}"
            "${NEXUS_SOURCE}/os/freertos/include"
            "${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime"
            "${NEXUS_SOURCE}/ext/freertos/include"
            "${NEXUS_SOURCE}/ext/freertos/portable/ThirdParty/GCC/Posix"
            "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime"
    )
    target_link_libraries(
        ${target}
        PRIVATE
            Nexus::Platform
            Nexus::SpiOwner
            Nexus::OSWait
            nexus_owned_warnings
            nexus_build_options
    )
endforeach()
target_compile_features(spi_bootstrap_test PRIVATE cxx_std_17)
set_target_properties(spi_bootstrap_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(spi_bootstrap_test PRIVATE GTest::gmock_main)
foreach(
    symbol
    IN
    ITEMS
        nx_platform_start
        nx_platform_stop
        nx_os_run
        nx_task_start
        nx_task_join_until
        nx_os_now_us
        nx_sleep_until
        nx_fault_observe
)
    target_link_options(spi_bootstrap_test PRIVATE "LINKER:--wrap=${symbol}")
endforeach()
add_test(
    NAME spi_bootstrap_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:spi_bootstrap_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/spi-bootstrap-google-execution"
)
set_tests_properties(
    spi_bootstrap_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
