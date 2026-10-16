cmake_minimum_required(VERSION 3.31)
project(nexus_examples VERSION 1.0.0 LANGUAGES C ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The SDK owns reusable mechanisms. This repository owns every application,
# entry point, task, domain mapping and recovery decision.
set(NEXUS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/external/nexus" CACHE PATH
    "Pinned Nexus source checkout or explicit relocated source SDK")
get_filename_component(NEXUS_SOURCE "${NEXUS_SOURCE}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${NEXUS_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize external/nexus recursively")
endif()
set(NEXUS_BUILD_TESTS OFF CACHE BOOL "SDK host development tests" FORCE)
set(NEXUS_BUILD_WORKLOAD OFF CACHE BOOL "SDK minimal workloads" FORCE)
add_subdirectory("${NEXUS_SOURCE}" nexus EXCLUDE_FROM_ALL)
get_target_property(soc_family Nexus::Platform NEXUS_SOC_FAMILY)

nexus_add_firmware(platform_reference SOURCES reproduction/main.c
    reproduction/rx_policy.c)
nexus_add_firmware(uart_owner_runtime
    SOURCES apps/uart_owner_runtime/main.c apps/uart_owner_runtime/lifecycle.c
        apps/uart_owner_runtime/bootstrap.c
        apps/uart_owner_runtime/scheduler_return.c
        apps/uart_owner_runtime/quarantine_policy.c
        apps/uart_owner_runtime/fault_policy.c
        apps/uart_owner_runtime/fault_observer.c
    LIBRARIES Nexus::UARTOwner Nexus::Arch)
if(soc_family STREQUAL "native")
    enable_testing()
    nexus_add_firmware(sensor_spi SOURCES apps/sensor_spi/main.c
        LIBRARIES Nexus::BMP280SPI)
    nexus_add_firmware(journal SOURCES apps/journal/main.c
        LIBRARIES Nexus::Storage Nexus::FileFlash)
    nexus_add_firmware(modbus_registers SOURCES apps/modbus_registers/main.c
        LIBRARIES Nexus::ModbusRTU Nexus::Log)
    add_subdirectory(tests)
    add_test(NAME uart_owner_shutdown COMMAND uart_owner_runtime)
    add_test(NAME platform_static_ports COMMAND platform_reference)
    add_test(NAME sensor_explicit_spi COMMAND sensor_spi)
    add_test(NAME persistent_journal COMMAND journal "${CMAKE_CURRENT_BINARY_DIR}/journal.dat")
    add_test(NAME protocol_authorized_registers COMMAND modbus_registers)
    set_tests_properties(platform_static_ports sensor_explicit_spi persistent_journal
        protocol_authorized_registers uart_owner_shutdown PROPERTIES TIMEOUT 10 LABELS "native;example")
endif()
