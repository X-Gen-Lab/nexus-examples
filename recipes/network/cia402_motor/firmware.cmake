# Optional product composition; the selected assembly owns its dependencies.
# Author: Nexus Team
include_guard(GLOBAL)
if(NOT TARGET example_motor_drive)
    include(
        "${CMAKE_CURRENT_LIST_DIR}/../../control/motor_drive/firmware.cmake"
    )
endif()
if(NOT TARGET Nexus::CiA402)
    message(FATAL_ERROR "Select components=[\"cia402\"] in this assembly")
endif()
add_library(
    example_cia402_motor
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/cia402_motor.c"
)
nexus_apply_target_policy(example_cia402_motor)
target_include_directories(
    example_cia402_motor
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_cia402_motor
    PUBLIC example_motor_drive Nexus::CiA402
    PRIVATE nexus_owned_warnings nexus_build_options
)
