cmake_minimum_required(VERSION 3.31)
project(nexus_usb_configuration LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(
    usb_examples_root
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    ABSOLUTE
)
if(NOT NEXUS_SOURCE)
    set(NEXUS_SOURCE "${usb_examples_root}/external/nexus")
endif()
if(CMAKE_CROSSCOMPILING)
    set(NEXUS_ASSEMBLY_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml"
        CACHE FILEPATH
        "Owned software-only USB pin and configuration recipe"
    )
else()
    set(NEXUS_TEST_SUPPORT
        ON
        CACHE BOOL
        "Explicit Native public-port model"
        FORCE
    )
    set(NEXUS_ASSEMBLY_FILE
        "${usb_examples_root}/configs/native.toml"
        CACHE FILEPATH
        "Native public-port contract harness"
    )
endif()
set(NEXUS_BUILD_TESTS OFF CACHE BOOL "Host SDK gates are independent" FORCE)
set(NEXUS_BUILD_WORKLOAD
    OFF
    CACHE BOOL
    "Application owns the entry point"
    FORCE
)
add_subdirectory("${NEXUS_SOURCE}" nexus EXCLUDE_FROM_ALL)
if(CMAKE_CROSSCOMPILING)
    nexus_add_firmware(usb_configuration SOURCES main.c usb_configuration.c
        LIBRARIES Nexus::USBControl Nexus::USBDescriptors Nexus::Arch
    )
    nexus_target_warnings(usb_configuration STRICT)
else()
    enable_testing()
    include("${usb_examples_root}/tests/cmake/GoogleTest.cmake")
    examples_host_google_test()
    include("${usb_examples_root}/tests/apps/usb_configuration/CMakeLists.txt")
endif()
