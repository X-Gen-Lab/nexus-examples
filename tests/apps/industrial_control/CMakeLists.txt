# The product consumer uses the same real SPI owner and POSIX wait backend.
include("${CMAKE_CURRENT_LIST_DIR}/control_loop.cmake")
add_executable(
    industrial_control_test
    "${CMAKE_CURRENT_LIST_DIR}/industrial_control_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/industrial_spi.c"
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/industrial_application.c"
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/native/model.c"
)
target_include_directories(
    industrial_control_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/industrial_control"
)
target_compile_features(industrial_control_test PRIVATE cxx_std_17)
set_target_properties(industrial_control_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    industrial_control_test
    PRIVATE
        Nexus::Platform
        Nexus::NativeModel
        Nexus::SpiOwner
        Nexus::Settings
        Nexus::ModbusRTU
        Nexus::BMP280
        Nexus::OS
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
target_link_options(
    industrial_control_test
    PRIVATE
        "LINKER:--wrap=nx_platform_start"
        "LINKER:--wrap=nx_settings_close"
        "LINKER:--wrap=nx_task_join_until"
        "LINKER:--wrap=nx_bus_owner_service"
        "LINKER:--wrap=nx_notify_port"
        "LINKER:--wrap=nx_native_uart_model_receive"
)
add_test(
    NAME industrial_control_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:industrial_control_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/industrial-google-execution"
)
set_tests_properties(
    industrial_control_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
