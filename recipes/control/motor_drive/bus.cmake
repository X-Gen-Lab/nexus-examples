# Optional voltage handoff; I2C exists only in the explicit INA226 adapter.
# Author: Nexus Team
include_guard(GLOBAL)
add_library(
    example_motor_bus
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/bus.c"
)
target_include_directories(example_motor_bus PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(
    example_motor_bus
    PUBLIC Nexus::Core
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_motor_bus)
add_library(
    example_ina226_bus
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/bus_ina226.c"
)
target_link_libraries(
    example_ina226_bus
    PUBLIC example_motor_bus Nexus::INA226
    PRIVATE Nexus::Arch nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_ina226_bus)
