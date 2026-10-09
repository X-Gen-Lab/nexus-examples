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
set(NEXUS_BUILD_CONTRACTS OFF CACHE BOOL "SDK in-tree link fixtures" FORCE)
add_subdirectory("${NEXUS_SOURCE}" nexus)

get_target_property(nexus_resolved_configuration Nexus::Config NEXUS_CONFIG_CMAKE)
include("${nexus_resolved_configuration}")
nexus_add_application(TARGET boot_blinky VERSION "0.1.0"
    SOURCES apps/boot_blinky/main.c)

if(CONFIG_OSAL_NATIVE OR CONFIG_OSAL_FREERTOS)
    nexus_add_application(TARGET rtos_pipeline VERSION "0.1.0"
        SOURCES apps/rtos_pipeline/main.c)
endif()

if(CONFIG_FRAMEWORK_CONFIG)
    nexus_add_application(TARGET config_roundtrip VERSION "0.1.0"
        SOURCES apps/config_roundtrip/main.c apps/config_roundtrip/heap_bounds.c
        EXTRA_DEPS Nexus::ConfigCore Nexus::ConfigRAM)
    if(CONFIG_PLATFORM_NATIVE)
        target_sources(config_roundtrip PRIVATE apps/config_roundtrip/heap_bounds_test.c)
    else()
        target_sources(config_roundtrip PRIVATE apps/config_roundtrip/newlib_sbrk.c)
        target_link_libraries(config_roundtrip PRIVATE Nexus::Arch)
    endif()
endif()

# SPI reference wiring is qualified in software only for Native/Discovery.
if(CONFIG_PLATFORM_NATIVE OR
   (CONFIG_BOARD_STM32F4DISCOVERY AND CONFIG_STM32_SPI_ENABLE AND CONFIG_STM32_SPI1_ENABLE))
    nexus_add_application(TARGET spi_transaction VERSION "0.1.0"
        SOURCES apps/spi_transaction/main.c EXTRA_DEPS Nexus::HALDevice)
    if(CONFIG_PLATFORM_NATIVE)
        target_compile_definitions(spi_transaction PRIVATE
            NEXUS_EXAMPLE_SPI_DEVICE="SPI0" NEXUS_EXAMPLE_SPI_CS=3)
    else()
        target_compile_definitions(spi_transaction PRIVATE
            NEXUS_EXAMPLE_SPI_DEVICE="SPI1" NEXUS_EXAMPLE_SPI_CS=0)
    endif()
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
    nexus_add_application(TARGET shell_console VERSION "0.1.0"
        SOURCES apps/shell_console/main.c EXTRA_DEPS Nexus::Shell)
    add_test(NAME boot_blinky_finite COMMAND boot_blinky --cycles 2)
    add_test(NAME rtos_pipeline_finite COMMAND rtos_pipeline --run-ms 400)
    add_test(NAME industrial_protocol_and_fault COMMAND industrial_model)
    add_test(NAME shell_console_finite COMMAND shell_console --smoke)
    add_test(NAME config_roundtrip_finite COMMAND config_roundtrip)
    add_test(NAME spi_transaction_finite COMMAND spi_transaction)
    set_tests_properties(boot_blinky_finite rtos_pipeline_finite
        industrial_protocol_and_fault shell_console_finite config_roundtrip_finite spi_transaction_finite
        PROPERTIES TIMEOUT 15 LABELS "native;example")
else()
    nexus_add_application(TARGET uart_echo VERSION "0.1.0"
        SOURCES apps/uart_echo/main.c)
    if(CONFIG_FRAMEWORK_LOG)
        nexus_add_application(TARGET log_uart VERSION "0.1.0"
            SOURCES apps/log_uart/main.c EXTRA_DEPS Nexus::LogRuntime Nexus::LogUART)
    endif()
    if(CONFIG_BOARD_STM32F4DISCOVERY)
        target_compile_definitions(uart_echo PRIVATE
            NEXUS_EXAMPLE_CONSOLE_DEVICE="UART1")
        if(TARGET log_uart)
            target_compile_definitions(log_uart PRIVATE NEXUS_EXAMPLE_CONSOLE_DEVICE="UART1")
        endif()
    else()
        target_compile_definitions(uart_echo PRIVATE
            NEXUS_EXAMPLE_CONSOLE_DEVICE="UART0")
        if(TARGET log_uart)
            target_compile_definitions(log_uart PRIVATE NEXUS_EXAMPLE_CONSOLE_DEVICE="UART0")
        endif()
    endif()
endif()
