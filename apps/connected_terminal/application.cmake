# Explicit task composition; no default heap, task or network worker.
# Author: Nexus Team
nexus_add_lwip()
add_library(
    connected_terminal_application
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/terminal_application.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../recipes/ui/display_reference.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../recipes/control/touch_reference.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../recipes/network/route_reference.c"
)
target_include_directories(
    connected_terminal_application
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    connected_terminal_application
    PUBLIC
        Nexus::ILI9341
        Nexus::XPT2046
        Nexus::Settings
        Nexus::LwIPManagedServices
        Nexus::WallClock
    PRIVATE nexus_owned_warnings nexus_build_options
)
