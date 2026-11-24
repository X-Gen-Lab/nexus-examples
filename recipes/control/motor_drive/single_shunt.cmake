# Explicit DC-link leaf; the default phase library never references this path.
# Author: Nexus Team
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/rotor.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/bus.cmake")
add_library(
    example_motor_drive_single
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/control.c"
    "${CMAKE_CURRENT_LIST_DIR}/single_shunt.c"
    "${CMAKE_CURRENT_LIST_DIR}/observation.c"
)
target_include_directories(
    example_motor_drive_single
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_motor_drive_single
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
nexus_apply_target_policy(example_motor_drive_single)
