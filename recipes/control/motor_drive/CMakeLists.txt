cmake_minimum_required(VERSION 3.31)
project(nexus_motor_drive_reference LANGUAGES C ASM)
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_LIST_DIR}/assembly.toml")
include("${CMAKE_CURRENT_LIST_DIR}/firmware.cmake")
nexus_add_firmware(motor_drive_reference SOURCES main.c
    LIBRARIES example_motor_drive
)
nexus_target_warnings(motor_drive_reference STRICT)
