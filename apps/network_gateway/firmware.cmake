# One composition is shared by the existing source and installed-SDK projects.
# The host peer is an explicit child tool; firmware/common platform stays C11.
get_target_property(gateway_family Nexus::Platform NEXUS_SOC_FAMILY)
if(NOT gateway_family STREQUAL "native")
    message(
        FATAL_ERROR
        "The gateway host reference requires the Native backend"
    )
endif()
nexus_add_lwip()
nexus_add_coremqtt()
nexus_add_tls()
nexus_add_firmware(network_gateway SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/native/host.c"
    "${CMAKE_CURRENT_LIST_DIR}/gateway_application.c"
    "${CMAKE_CURRENT_LIST_DIR}/gateway_policy.c"
    "${CMAKE_CURRENT_LIST_DIR}/native/native_frames.c"
    "${CMAKE_CURRENT_LIST_DIR}/native/native_runtime.c"
    "${CMAKE_CURRENT_LIST_DIR}/native/host_peer.c"
    LIBRARIES Nexus::LwIPStorage Nexus::LwIPTCPStorage
        Nexus::TLSStorage Nexus::CoreMQTTStorage Nexus::IOWatchdog
)
