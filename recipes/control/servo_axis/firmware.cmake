# The sensor-only application and shared independent DC plant stay separate.
# Author: Nexus Team
add_library(
    example_servo_axis
    STATIC
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/servo_control.c"
    "${CMAKE_CURRENT_LIST_DIR}/servo_sil.c"
    "${CMAKE_CURRENT_LIST_DIR}/../differential_drive/sil_model.c"
)
target_include_directories(
    example_servo_axis
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/../differential_drive"
)
target_link_libraries(
    example_servo_axis
    PUBLIC Nexus::AxisControl Nexus::Algorithms
    PRIVATE nexus_owned_warnings nexus_build_options
)
nexus_apply_target_policy(example_servo_axis)
