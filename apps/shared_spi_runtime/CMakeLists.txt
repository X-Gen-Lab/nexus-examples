cmake_minimum_required(VERSION 3.31)
project(nexus_shared_spi_runtime LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_CROSSCOMPILING)
    message(
        FATAL_ERROR
        "Use the root Native build for host; this is the F407 software fixture"
    )
endif()
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml")
nexus_add_firmware(shared_spi_runtime SOURCES main.c shared_spi_application.c
    LIBRARIES Nexus::SpiOwner Nexus::OSFreeRTOS Nexus::Arch
)
nexus_target_warnings(shared_spi_runtime STRICT)
