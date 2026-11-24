# Independent host model; the real MCU graph never links this plant.
# Author: Nexus Team
add_library(
    example_pmsm_plant
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/plant.c"
)
target_include_directories(
    example_pmsm_plant
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_pmsm_plant
    PUBLIC Nexus::Core
    PRIVATE nexus_owned_warnings nexus_build_options
)
if(NOT MSVC)
    target_link_libraries(example_pmsm_plant PRIVATE m)
endif()
nexus_apply_target_policy(example_pmsm_plant)
add_library(
    example_dual_motor_drive
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/sil.c"
)
target_include_directories(
    example_dual_motor_drive
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_dual_motor_drive
    PUBLIC example_pmsm_plant example_motor_drive
    PRIVATE nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_dual_motor_drive)
