cmake_minimum_required(VERSION 3.31)
project(nexus_starter LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(NEXUS_ASSEMBLY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml"
    CACHE FILEPATH
    "This product owns its sole assembly"
)

# Nexus_DIR selects a verified installed SDK. NEXUS_SOURCE is an explicit
# development checkout; its outputs do not acquire release qualification.
if(NEXUS_SOURCE)
    set(NEXUS_BUILD_TESTS OFF CACHE BOOL "Platform development tests" FORCE)
    set(NEXUS_BUILD_WORKLOAD OFF CACHE BOOL "Platform workload" FORCE)
    add_subdirectory("${NEXUS_SOURCE}" nexus EXCLUDE_FROM_ALL)
else()
    find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
endif()
nexus_add_firmware(nexus_starter SOURCES main.c)
get_target_property(family Nexus::Platform NEXUS_SOC_FAMILY)
if(family STREQUAL "native")
    enable_testing()
    add_test(NAME starter_application COMMAND nexus_starter)
    set_tests_properties(starter_application PROPERTIES TIMEOUT 10)
endif()
