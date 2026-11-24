cmake_minimum_required(VERSION 3.31)
project(nexus_motor_drive_reference LANGUAGES C ASM)
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
set(EXAMPLE_MOTOR_CURRENT_PATH
    "phase"
    CACHE STRING
    "Selected phase or single leaf"
)
set_property(CACHE EXAMPLE_MOTOR_CURRENT_PATH PROPERTY STRINGS phase single)
if(EXAMPLE_MOTOR_CURRENT_PATH STREQUAL "phase")
    set(_motor_assembly "assembly.toml")
    set(_motor_library example_motor_drive)
    set(_motor_leaf "firmware.cmake")
    set(_motor_target motor_drive_reference)
elseif(EXAMPLE_MOTOR_CURRENT_PATH STREQUAL "single")
    set(_motor_assembly "single.toml")
    set(_motor_library example_motor_drive_single)
    set(_motor_leaf "single_shunt.cmake")
    set(_motor_target motor_drive_single_reference)
else()
    message(FATAL_ERROR "EXAMPLE_MOTOR_CURRENT_PATH must be phase or single")
endif()
if(DEFINED NEXUS_ASSEMBLY_FILE)
    get_filename_component(_motor_requested "${NEXUS_ASSEMBLY_FILE}" REALPATH)
    get_filename_component(
        _motor_expected
        "${CMAKE_CURRENT_LIST_DIR}/${_motor_assembly}"
        REALPATH
    )
    if(NOT _motor_requested STREQUAL _motor_expected)
        message(
            FATAL_ERROR
            "Requested assembly does not match the selected motor current path"
        )
    endif()
endif()
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_LIST_DIR}/${_motor_assembly}")
include("${CMAKE_CURRENT_LIST_DIR}/${_motor_leaf}")
nexus_add_firmware(${_motor_target} SOURCES main.c
    LIBRARIES ${_motor_library}
)
if(EXAMPLE_MOTOR_CURRENT_PATH STREQUAL "single")
    target_compile_definitions(${_motor_target} PRIVATE EXAMPLE_SINGLE_SHUNT=1)
endif()
nexus_target_warnings(${_motor_target} STRICT)
