# Explicit source-checkout integration; private MMIO fixtures never enter SDKs.
# Author: Nexus Team
option(
    EXAMPLE_MOTOR_PROVIDER_MODEL
    "Build development F407 provider integration"
    OFF
)
if(NOT EXAMPLE_MOTOR_PROVIDER_MODEL)
    return()
endif()
if(NOT EXISTS "${NEXUS_SOURCE}/.git" OR CMAKE_CROSSCOMPILING)
    message(
        FATAL_ERROR
        "Motor provider model requires an explicit host source checkout"
    )
endif()
set(_motor_soc "${NEXUS_SOURCE}/soc/stm32/stm32f4")
set(_motor_model "${NEXUS_SOURCE}/tests/google/soc")
include("${NEXUS_SOURCE}/tests/cmake/CMSISHostModel.cmake")
include("${NEXUS_SOURCE}/cmake/platform/CMSISCore.cmake")
include("${_motor_soc}/sdk/STM32Official.cmake")
add_executable(
    motor_provider_test
    "${CMAKE_CURRENT_LIST_DIR}/provider_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/provider_model.cpp"
    "${_motor_soc}/drivers/pwm_adc/power_stage.c"
    "${_motor_soc}/drivers/pwm_adc/analog.c"
    "${_motor_soc}/drivers/pwm_adc/adc_pair.c"
    "${_motor_soc}/drivers/pwm_adc/single_shunt.c"
    "${_motor_soc}/drivers/pwm_adc/power_stage_dc.c"
    "${_motor_soc}/drivers/ip/av/av.c"
    "${_motor_soc}/drivers/timer/timer_waveform.c"
)
target_compile_features(motor_provider_test PRIVATE cxx_std_17)
set_target_properties(motor_provider_test PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(
    motor_provider_test
    PRIVATE STM32F407xx NEXUS_STM32_MODEL=1
)
target_include_directories(
    motor_provider_test
    PRIVATE "${_motor_soc}" "${_motor_model}" "${NEXUS_SOURCE}/io/private"
)
target_include_directories(
    motor_provider_test
    SYSTEM
    PRIVATE "${NEXUS_SOURCE}/vendors/st/cmsis_device_f4/Include"
)
target_link_libraries(
    motor_provider_test
    PRIVATE
        example_motor_drive
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
        Nexus::IOStream
        Nexus::Arch
)
function(_example_motor_provider_inputs target)
    set(NEXUS_SOURCE_DIR "${NEXUS_SOURCE}")
    nexus_stm32f4_official(${target})
endfunction()
_example_motor_provider_inputs(motor_provider_test)
nexus_use_cmsis_core(motor_provider_test)
nexus_use_cmsis_host_model(motor_provider_test)
target_compile_options(
    motor_provider_test
    PRIVATE
        "-include${_motor_model}/f407_timer_waveform_model.h"
        -ffunction-sections
        -fdata-sections
)
target_link_options(motor_provider_test PRIVATE -Wl,--gc-sections)
add_test(
    NAME motor_provider_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:motor_provider_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/motor-provider-google-execution"
)
set_tests_properties(
    motor_provider_google
    PROPERTIES
        TIMEOUT 30
        LABELS "native;example;GoogleTest;development-provider"
)
