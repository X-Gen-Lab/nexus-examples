cmake_minimum_required(VERSION 3.31)
project(nexus_stream_windows LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Use the root Native application contract build")
endif()
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
if(NOT NEXUS_ASSEMBLY_FILE)
    message(
        FATAL_ERROR
        "Select adc-window.toml or audio-window.toml explicitly"
    )
endif()
nexus_add_platform(ASSEMBLY "${NEXUS_ASSEMBLY_FILE}")

# Explicit targets link their own complete application and selected assembly.
nexus_add_firmware(adc_window SOURCES adc_window_main.c adc_acquisition.c)
nexus_add_firmware(audio_window SOURCES audio_window_main.c audio_acquisition.c)
set_target_properties(adc_window audio_window PROPERTIES EXCLUDE_FROM_ALL TRUE)
nexus_target_warnings(adc_window STRICT)
nexus_target_warnings(audio_window STRICT)
