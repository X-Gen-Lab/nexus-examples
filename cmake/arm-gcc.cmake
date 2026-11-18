# Select the SDK before project() and preserve it in compiler probes.
if(
    NOT NEXUS_SOURCE
    OR NOT EXISTS "${NEXUS_SOURCE}/cmake/toolchains/arm-gcc.cmake"
)
    message(FATAL_ERROR "Select a complete Nexus source SDK with NEXUS_SOURCE")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES NEXUS_SOURCE)
include("${NEXUS_SOURCE}/cmake/toolchains/arm-gcc.cmake")
