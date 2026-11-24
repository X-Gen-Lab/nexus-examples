# Optional absolute-angle source; only selected consumers pay its storage/code.
# Author: Nexus Team
include_guard(GLOBAL)
add_library(
    example_motor_rotor
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/rotor.c"
)
target_include_directories(
    example_motor_rotor
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_motor_rotor
    PUBLIC Nexus::Core
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_motor_rotor)
add_library(
    example_as5047p_rotor
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/as5047p_rotor.c"
)
target_link_libraries(
    example_as5047p_rotor
    PUBLIC example_motor_rotor Nexus::AS5047P Nexus::Algorithms
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_as5047p_rotor)
