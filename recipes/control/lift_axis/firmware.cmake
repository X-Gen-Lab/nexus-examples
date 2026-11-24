# No provider, GPIO worker or product safety policy is hidden in this leaf.
# Author: Nexus Team
if(NOT TARGET example_lift_axis)
    add_library(
        example_lift_axis
        STATIC
        "${CMAKE_CURRENT_LIST_DIR}/lift_axis.c"
    )
    target_include_directories(
        example_lift_axis
        PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_link_libraries(
        example_lift_axis
        PUBLIC Nexus::AxisControl
        PRIVATE nexus_owned_warnings nexus_build_options
    )
endif()
if(NOT TARGET example_lift_model)
    add_library(
        example_lift_model
        STATIC
        "${CMAKE_CURRENT_LIST_DIR}/native/model.c"
    )
    target_link_libraries(
        example_lift_model
        PUBLIC example_lift_axis
        PRIVATE nexus_owned_warnings nexus_build_options
    )
endif()
