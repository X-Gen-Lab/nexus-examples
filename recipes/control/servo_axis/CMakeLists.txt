cmake_minimum_required(VERSION 3.31)
project(nexus_servo_axis LANGUAGES C ASM)
set(NEXUS_TEST_SUPPORT ON CACHE BOOL "Explicit Native software-model scope")
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_LIST_DIR}/assembly.toml")
get_target_property(_servo_soc Nexus::Platform NEXUS_SOC_FAMILY)
if(NOT _servo_soc STREQUAL "native")
    message(
        FATAL_ERROR
        "The loaded servo experiment is a Native software model"
    )
endif()
include("${CMAKE_CURRENT_LIST_DIR}/firmware.cmake")
nexus_add_firmware(servo_axis_reference SOURCES main.c
    LIBRARIES example_servo_axis
)
enable_testing()
add_test(NAME servo_axis_software COMMAND servo_axis_reference)
set_tests_properties(servo_axis_software PROPERTIES TIMEOUT 10)
