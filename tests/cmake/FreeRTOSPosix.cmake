# Optional application integration with the platform's development kernel seam.
# Installed SDK consumers never acquire this private platform test dependency.
option(NEXUS_EXAMPLES_FREERTOS_POSIX_TESTS
    "Run source-only application tests with the platform POSIX FreeRTOS seam" OFF)
if(NOT NEXUS_EXAMPLES_FREERTOS_POSIX_TESTS)
    return()
endif()
if(EXISTS "${NEXUS_SOURCE}/.nexus-source-sdk.json" OR
        NOT EXISTS "${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime/CMakeLists.txt")
    message(FATAL_ERROR
        "FreeRTOS POSIX application tests require a source checkout with its development kernel harness")
endif()

find_package(Threads REQUIRED)
set(NEXUS_SOURCE_DIR "${NEXUS_SOURCE}")
add_subdirectory("${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime"
    freertos-kernel EXCLUDE_FROM_ALL)
add_executable(shared_spi_freertos_host
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/main.c"
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/shared_spi_application.c")
target_compile_definitions(shared_spi_freertos_host PRIVATE EXAMPLE_FREERTOS_HOST=1)
target_link_libraries(shared_spi_freertos_host PRIVATE Nexus::Platform
    Nexus::SpiOwner Nexus::Arch contract_os_freertos_posix
    nexus_owned_warnings nexus_build_options)
add_dependencies(shared_spi_freertos_host contract_os_freertos)
add_test(NAME shared_spi_real_freertos COMMAND shared_spi_freertos_host)
set_tests_properties(shared_spi_real_freertos PROPERTIES TIMEOUT 30
    LABELS "native;example;FreeRTOS;source-only")
