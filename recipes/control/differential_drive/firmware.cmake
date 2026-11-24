# A finite host-only experiment shares the actual application source graph.
# Author: Nexus Team
add_library(
    example_differential_drive
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/drive.c"
    "${CMAKE_CURRENT_LIST_DIR}/sil.c"
    "${CMAKE_CURRENT_LIST_DIR}/sil_model.c"
)
target_include_directories(
    example_differential_drive
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_differential_drive
    PUBLIC Nexus::AxisControl Nexus::Algorithms
    PRIVATE nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_differential_drive)
