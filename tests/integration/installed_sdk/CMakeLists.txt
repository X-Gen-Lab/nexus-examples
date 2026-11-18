cmake_minimum_required(VERSION 3.31)
project(nexus_installed_reference LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# This consumer entry reuses application sources from the examples checkout.
# The installed SDK remains a separately verified dependency.
get_filename_component(
    examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
find_package(Nexus CONFIG REQUIRED)
nexus_add_firmware(nexus_firmware SOURCES
    "${examples_root}/apps/platform_reference/main.c"
    "${examples_root}/apps/platform_reference/rx_policy.c"
)
get_target_property(soc_family Nexus::Platform NEXUS_SOC_FAMILY)
if(soc_family STREQUAL "native")
    enable_testing()
    target_link_libraries(nexus_firmware PRIVATE Nexus::NativeModel)
    include("${examples_root}/apps/industrial_control/firmware.cmake")
    # The test's sensor deadlines use model time while the two tasks run on POSIX.
    target_sources(
        industrial_control
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/industrial_clock.c"
    )
    target_link_options(
        industrial_control
        PRIVATE
            "LINKER:--wrap=example_industrial_model_clock"
            "LINKER:--wrap=nx_native_notify_port"
    )
    include("${examples_root}/apps/network_gateway/firmware.cmake")
    add_test(NAME installed_platform_reference COMMAND nexus_firmware)
    add_test(
        NAME installed_industrial_control
        COMMAND
            industrial_control
            "${CMAKE_CURRENT_BINARY_DIR}/industrial-model.dat"
    )
    set_tests_properties(
        installed_platform_reference
        installed_industrial_control
        PROPERTIES TIMEOUT 10
    )
endif()
