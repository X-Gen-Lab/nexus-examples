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
if(CMAKE_CROSSCOMPILING)
    set(usb_assembly "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml")
else()
    set(NEXUS_TEST_SUPPORT ON CACHE BOOL "Explicit Native public-port model")
    set(usb_assembly "${usb_examples_root}/configs/native.toml")
endif()
find_package(
    Nexus
    0.2.1
    EXACT
    CONFIG
    REQUIRED
    PATHS "${usb_examples_root}/external/nexus/cmake"
)
nexus_add_platform(ASSEMBLY "${usb_assembly}")
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
