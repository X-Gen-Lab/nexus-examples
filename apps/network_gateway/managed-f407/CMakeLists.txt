# Opt-in ARM link of the managed gateway against the reviewed software Board.
cmake_minimum_required(VERSION 3.31)
if(NOT NEXUS_SOURCE)
    get_filename_component(
        default_source
        "${CMAKE_CURRENT_LIST_DIR}/../../../external/nexus"
        ABSOLUTE
    )
    set(NEXUS_SOURCE "${default_source}" CACHE PATH "Exact Nexus source SDK")
endif()
get_filename_component(NEXUS_SOURCE "${NEXUS_SOURCE}" REALPATH)
project(managed_gateway_f407 LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(
    Nexus
    0.2.1
    EXACT
    CONFIG
    REQUIRED
    PATHS "${NEXUS_SOURCE}/cmake"
    NO_DEFAULT_PATH
)
if(NOT Nexus_SOURCE_DIR STREQUAL NEXUS_SOURCE)
    message(FATAL_ERROR "Nexus_DIR differs from the selected SDK")
endif()
nexus_add_platform(
    ASSEMBLY "${NEXUS_SOURCE}/tests/contracts/f407_qiming_network/assembly.toml"
)
nexus_add_lwip()
nexus_add_tls()
nexus_add_coremqtt()
nexus_add_flashdb()
set(gateway "${CMAKE_CURRENT_LIST_DIR}/..")
set(network "${CMAKE_CURRENT_LIST_DIR}/../../../recipes/network")
set(secure "${NEXUS_SOURCE}/cmake/package/verification/secure_services")
nexus_add_firmware(managed_gateway_f407 SOURCES
    main.c "${gateway}/gateway_application.c" "${gateway}/gateway_policy.c"
    "${gateway}/gateway_delivery.c" "${gateway}/gateway_managed.c"
    "${network}/route_reference.c" "${network}/delivery_reference.c"
    LIBRARIES Nexus::LwIPManagedTCPStorage Nexus::LwIPManagedServicesStorage
        Nexus::TLSStorage Nexus::CoreMQTTStorage Nexus::FlashDB Nexus::Algorithms
        Nexus::WallClock Nexus::IOWatchdog
)
target_include_directories(
    managed_gateway_f407
    PRIVATE "${gateway}" "${network}" "${secure}"
)
nexus_target_warnings(managed_gateway_f407 STRICT)
