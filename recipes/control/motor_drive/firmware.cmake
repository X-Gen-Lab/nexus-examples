# One public-interface bridge owns its callback and session storage.
# Author: Nexus Team
include("${CMAKE_CURRENT_LIST_DIR}/rotor.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/bus.cmake")
add_library(
    example_motor_drive
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/control.c"
    "${CMAKE_CURRENT_LIST_DIR}/phase_path.c"
    "${CMAKE_CURRENT_LIST_DIR}/observation.c"
    "${CMAKE_CURRENT_LIST_DIR}/telemetry.c"
)
target_include_directories(
    example_motor_drive
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
set_property(
    TARGET example_motor_drive
    PROPERTY INTERFACE_EXAMPLE_MOTOR_CURRENT_PATH phase
)
set_property(
    TARGET example_motor_drive
    APPEND
    PROPERTY COMPATIBLE_INTERFACE_STRING EXAMPLE_MOTOR_CURRENT_PATH
)
target_link_libraries(
    example_motor_drive
    PUBLIC
        Nexus::DriveControl
        Nexus::CurrentControl
        Nexus::ModulationControl
        Nexus::IOPowerStage
        Nexus::Algorithms
        example_motor_rotor
        example_motor_bus
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_motor_drive)
