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
if(NOT NEXUS_ASSEMBLY_FILE)
    set(NEXUS_ASSEMBLY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml")
endif()
nexus_add_platform(ASSEMBLY "${NEXUS_ASSEMBLY_FILE}")
nexus_add_firmware(shared_spi_runtime SOURCES main.c shared_spi_application.c
    LIBRARIES Nexus::SpiOwner Nexus::OS Nexus::Arch
)
nexus_target_warnings(shared_spi_runtime STRICT)
nexus_add_firmware(finite_spi_dma SOURCES finite_dma_main.c finite_dma.c)
set_target_properties(finite_spi_dma PROPERTIES EXCLUDE_FROM_ALL TRUE)
nexus_target_warnings(finite_spi_dma STRICT)
