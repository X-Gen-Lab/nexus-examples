cmake_minimum_required(VERSION 3.21)
project(nexus_examples VERSION 0.1.0 LANGUAGES C ASM)

# The application owns its entry point and composition. Nexus remains a pinned
# source SDK; no HAL, OSAL or vendor implementation is copied into this project.
set(NEXUS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/external/nexus" CACHE PATH
    "Path to the pinned Nexus source SDK")
get_filename_component(NEXUS_SOURCE "${NEXUS_SOURCE}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${NEXUS_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize external/nexus recursively or pass -DNEXUS_SOURCE=/absolute/sdk/path")
endif()
set(NEXUS_BUILD_TESTS OFF CACHE BOOL "SDK development tests" FORCE)
set(NEXUS_BUILD_EXAMPLES OFF CACHE BOOL "SDK in-tree examples" FORCE)
add_subdirectory("${NEXUS_SOURCE}" nexus)

get_target_property(nexus_resolved_configuration Nexus::Config NEXUS_CONFIG_CMAKE)
include("${nexus_resolved_configuration}")
nexus_add_application(TARGET boot_blinky VERSION "0.1.0"
    SOURCES apps/boot_blinky/main.c)

if(CONFIG_OSAL_NATIVE OR CONFIG_OSAL_FREERTOS)
    nexus_add_application(TARGET rtos_pipeline VERSION "0.1.0"
        SOURCES apps/rtos_pipeline/main.c)
endif()

if(CONFIG_PLATFORM_NATIVE)
    add_executable(industrial_model apps/industrial_model/main.c)
    target_link_libraries(industrial_model PRIVATE
        Nexus::Config Nexus::ModbusRTU Nexus::Industrial)
    set_target_properties(industrial_model PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nexus/bin")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(industrial_model PRIVATE
            "-Wl,-Map,$<TARGET_FILE_DIR:industrial_model>/industrial_model.map")
    endif()
    enable_testing()
    add_test(NAME boot_blinky_finite COMMAND boot_blinky --cycles 2)
    add_test(NAME rtos_pipeline_finite COMMAND rtos_pipeline --run-ms 400)
    add_test(NAME industrial_protocol_and_fault COMMAND industrial_model)
    set_tests_properties(boot_blinky_finite rtos_pipeline_finite
        industrial_protocol_and_fault PROPERTIES TIMEOUT 15 LABELS "native;example")
else()
    nexus_add_application(TARGET uart_echo VERSION "0.1.0"
        SOURCES apps/uart_echo/main.c)
    if(CONFIG_BOARD_STM32F4DISCOVERY)
        target_compile_definitions(uart_echo PRIVATE
            NEXUS_EXAMPLE_CONSOLE_DEVICE="UART1")
    else()
        target_compile_definitions(uart_echo PRIVATE
            NEXUS_EXAMPLE_CONSOLE_DEVICE="UART0")
    endif()
endif()
