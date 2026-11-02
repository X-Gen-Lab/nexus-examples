cmake_minimum_required(VERSION 3.31)
project(nexus_shared_spi_runtime LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Use the root Native build for host; this is the F407 software fixture")
endif()
set(NEXUS_ASSEMBLY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml" CACHE FILEPATH
    "Authored software fixture assembly")
# The product owns this Board package in both source and installed SDK builds.
# Use separate build directories; Nexus_DIR verifies the installed SDK identity.
if(NEXUS_SOURCE)
    set(NEXUS_BUILD_TESTS OFF CACHE BOOL "Host tests are separate" FORCE)
    set(NEXUS_BUILD_WORKLOAD OFF CACHE BOOL "Application owns entry" FORCE)
    add_subdirectory("${NEXUS_SOURCE}" nexus EXCLUDE_FROM_ALL)
else()
    find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
endif()
nexus_add_firmware(shared_spi_runtime SOURCES main.c shared_spi_application.c
    LIBRARIES Nexus::SpiOwner Nexus::OSFreeRTOS Nexus::Arch)
nexus_target_warnings(shared_spi_runtime STRICT)
