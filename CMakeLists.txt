cmake_minimum_required(VERSION 3.31)
set(_nexus_default_source "${CMAKE_CURRENT_SOURCE_DIR}/external/nexus")
if(DEFINED ENV{NEXUS_SOURCE} AND NOT "$ENV{NEXUS_SOURCE}" STREQUAL "")
    get_filename_component(
        _nexus_environment_source
        "$ENV{NEXUS_SOURCE}"
        REALPATH
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set(_nexus_default_source "${_nexus_environment_source}")
endif()
set(NEXUS_SOURCE
    "${_nexus_default_source}"
    CACHE PATH
    "Pinned Nexus source checkout or explicit relocated source SDK"
)
get_filename_component(
    NEXUS_SOURCE
    "${NEXUS_SOURCE}"
    REALPATH
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(
    DEFINED _nexus_environment_source
    AND NOT NEXUS_SOURCE STREQUAL _nexus_environment_source
)
    message(
        FATAL_ERROR
        "NEXUS_SOURCE differs between the environment and CMake cache; "
        "select one SDK and use a separate build directory"
    )
endif()
if(NOT EXISTS "${NEXUS_SOURCE}/cmake/NexusConfig.cmake")
    message(FATAL_ERROR "Initialize external/nexus recursively")
endif()
project(nexus_examples VERSION 1.0.0 LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The SDK owns reusable mechanisms. This repository owns every application,
# entry point, task, domain mapping and recovery decision.
find_package(
    Nexus
    0.2.1
    EXACT
    CONFIG
    REQUIRED
    PATHS "${NEXUS_SOURCE}/cmake"
    NO_DEFAULT_PATH
)
if(NOT Nexus_SOURCE_DIR STREQUAL NEXUS_SOURCE)
    message(FATAL_ERROR "Nexus_DIR differs from the selected NEXUS_SOURCE")
endif()
nexus_add_platform(ASSEMBLY "${NEXUS_ASSEMBLY_FILE}")
get_target_property(soc_family Nexus::Platform NEXUS_SOC_FAMILY)

nexus_add_firmware(platform_reference SOURCES apps/platform_reference/main.c
    apps/platform_reference/rx_policy.c
)
nexus_add_firmware(uart_owner_runtime
    SOURCES apps/uart_owner_runtime/main.c apps/uart_owner_runtime/lifecycle.c
        apps/uart_owner_runtime/bootstrap.c
        apps/uart_owner_runtime/scheduler_return.c
        apps/uart_owner_runtime/quarantine_policy.c
        apps/uart_owner_runtime/fault_policy.c
        apps/uart_owner_runtime/fault_observer.c
    LIBRARIES Nexus::UARTOwner Nexus::Arch
)
if(soc_family STREQUAL "native")
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    enable_testing()
    nexus_add_firmware(sensor_spi SOURCES apps/sensor_spi/main.c
        LIBRARIES Nexus::BMP280SPI
    )
    nexus_add_firmware(journal SOURCES apps/journal/main.c
        LIBRARIES Nexus::Storage Nexus::FileFlash
    )
    nexus_add_firmware(modbus_registers SOURCES apps/modbus_registers/main.c
        LIBRARIES Nexus::ModbusRTU Nexus::Log
    )
    nexus_add_firmware(shared_spi_runtime SOURCES
        apps/shared_spi_runtime/main.c apps/shared_spi_runtime/shared_spi_application.c
        LIBRARIES Nexus::SpiOwner Nexus::OS Nexus::Arch
    )
    nexus_add_firmware(stream_recovery SOURCES
        apps/stream_recovery/main.c apps/stream_recovery/stream_application.c
    )
    include(
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/industrial_control/firmware.cmake"
    )
    add_subdirectory(tests)
    include("${CMAKE_CURRENT_SOURCE_DIR}/apps/network_gateway/firmware.cmake")
    foreach(
        model_target
        IN
        ITEMS
            platform_reference
            uart_owner_runtime
            sensor_spi
            modbus_registers
            shared_spi_runtime
            stream_recovery
    )
        target_link_libraries(${model_target} PRIVATE Nexus::NativeModel)
    endforeach()
    add_test(NAME shared_spi_two_devices COMMAND shared_spi_runtime)
    add_test(NAME stream_slow_consumer COMMAND stream_recovery)
    add_test(NAME uart_owner_shutdown COMMAND uart_owner_runtime)
    add_test(NAME platform_static_ports COMMAND platform_reference)
    add_test(NAME sensor_explicit_spi COMMAND sensor_spi)
    add_test(
        NAME persistent_journal
        COMMAND journal "${CMAKE_CURRENT_BINARY_DIR}/journal.dat"
    )
    add_test(NAME protocol_authorized_registers COMMAND modbus_registers)
    add_test(
        NAME industrial_control_reference
        COMMAND
            industrial_control
            "${CMAKE_CURRENT_BINARY_DIR}/industrial-model.dat"
    )
    set_tests_properties(
        platform_static_ports
        sensor_explicit_spi
        persistent_journal
        protocol_authorized_registers
        uart_owner_shutdown
        shared_spi_two_devices
        stream_slow_consumer
        industrial_control_reference
        PROPERTIES TIMEOUT 10 LABELS "native;example"
    )
endif()
