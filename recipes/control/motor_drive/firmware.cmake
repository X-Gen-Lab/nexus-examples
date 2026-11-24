# One public-interface bridge owns its callback and session storage.
# Author: Nexus Team
add_library(
    example_motor_drive
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/control.c"
    "${CMAKE_CURRENT_LIST_DIR}/observation.c"
)
target_include_directories(
    example_motor_drive
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_motor_drive
    PUBLIC Nexus::MotorControl Nexus::IOPowerStage Nexus::Algorithms
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_motor_drive)
