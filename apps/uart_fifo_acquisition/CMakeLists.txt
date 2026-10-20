# Public-only application entry; the SDK owns one resolved assembly and startup.
cmake_minimum_required(VERSION 3.31)
project(nexus_uart_fifo_acquisition LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(NEXUS_SOURCE "" CACHE PATH "Explicit complete pinned Nexus source SDK")
set(NEXUS_ASSEMBLY_FILE "" CACHE FILEPATH
    "Existing SDK FIFO reference assembly")
if(NOT NEXUS_SOURCE OR NOT NEXUS_ASSEMBLY_FILE)
    message(FATAL_ERROR "Set explicit NEXUS_SOURCE and NEXUS_ASSEMBLY_FILE")
endif()
file(REAL_PATH "${NEXUS_SOURCE}" NEXUS_SOURCE
    BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
file(REAL_PATH "${NEXUS_ASSEMBLY_FILE}" NEXUS_ASSEMBLY_FILE
    BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${NEXUS_SOURCE}/CMakeLists.txt" OR
   NOT EXISTS "${NEXUS_ASSEMBLY_FILE}")
    message(FATAL_ERROR "Explicit SDK source and assembly must exist")
endif()
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Pass the SDK ARM toolchain when configuring this app")
endif()

# Select existing authored facts; never copy or infer another Board/mode plan.
set(fifo_reference_selected FALSE)
set(fifo_reference_dir
    "${NEXUS_SOURCE}/cmake/package/verification/io_evolution")
foreach(reference h743 u575)
    file(REAL_PATH
        "${fifo_reference_dir}/${reference}-uart-fifo.toml"
        fifo_reference)
    if(NEXUS_ASSEMBLY_FILE STREQUAL fifo_reference)
        set(fifo_reference_selected TRUE)
    endif()
endforeach()
if(NOT fifo_reference_selected)
    message(FATAL_ERROR "Select the maintained H743 or U575 UART FIFO assembly")
endif()

set(NEXUS_BUILD_TESTS OFF CACHE BOOL
    "No host tests in this MCU application" FORCE)
set(NEXUS_BUILD_WORKLOAD OFF CACHE BOOL "Use this explicit application" FORCE)
add_subdirectory("${NEXUS_SOURCE}" nexus EXCLUDE_FROM_ALL)
nexus_add_firmware(uart_fifo_acquisition SOURCES main.c workload.c)
nexus_target_warnings(uart_fifo_acquisition STRICT)
