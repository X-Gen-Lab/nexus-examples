cmake_minimum_required(VERSION 3.31)
project(nexus_uart_owner_runtime LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_CROSSCOMPILING)
    message(
        FATAL_ERROR
        "Use the root Native build or the explicit ThreadX host contract"
    )
endif()
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
if(NOT NEXUS_ASSEMBLY_FILE)
    set(NEXUS_ASSEMBLY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/threadx.toml")
endif()
nexus_add_platform(ASSEMBLY "${NEXUS_ASSEMBLY_FILE}")
nexus_add_firmware(uart_owner_runtime
    SOURCES main.c lifecycle.c bootstrap.c scheduler_return.c quarantine_policy.c
        fault_policy.c fault_observer.c
    LIBRARIES Nexus::UARTOwner Nexus::OS Nexus::Arch
)
nexus_target_warnings(uart_owner_runtime STRICT)
