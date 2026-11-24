cmake_minimum_required(VERSION 3.31)
project(nexus_dual_pmsm LANGUAGES C ASM)
set(NEXUS_TEST_SUPPORT ON CACHE BOOL "Explicit Native software-model scope")
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_LIST_DIR}/assembly.toml")
get_target_property(_dual_soc Nexus::Platform NEXUS_SOC_FAMILY)
if(NOT _dual_soc STREQUAL "native")
    message(FATAL_ERROR "The independent dual PMSM plant is Native only")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/firmware.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../motor_drive/firmware.cmake")
nexus_add_firmware(dual_motor_drive_reference SOURCES main.c
    LIBRARIES example_dual_motor_drive
)
enable_testing()
add_test(NAME dual_motor_drive_software COMMAND dual_motor_drive_reference)
set_tests_properties(dual_motor_drive_software PROPERTIES TIMEOUT 10)
option(EXAMPLE_CONTROL_TESTS "Build the application GoogleTest contracts" OFF)
if(EXAMPLE_CONTROL_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake/GoogleTest.cmake")
    examples_host_google_test()
    add_executable(
        motor_drive_contract
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/motor_drive_test.cpp"
    )
    target_compile_features(motor_drive_contract PRIVATE cxx_std_17)
    set_target_properties(motor_drive_contract PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(
        motor_drive_contract
        PRIVATE
            example_motor_drive
            example_as5047p_rotor
            GTest::gmock_main
            nexus_owned_warnings
            nexus_build_options
    )
    enable_testing()
    add_test(NAME motor_drive_contract COMMAND motor_drive_contract)
    add_executable(
        dual_motor_contract
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/dual_motor_test.cpp"
    )
    target_compile_features(dual_motor_contract PRIVATE cxx_std_17)
    set_target_properties(dual_motor_contract PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(
        dual_motor_contract
        PRIVATE
            example_dual_motor_drive
            GTest::gmock_main
            nexus_owned_warnings
            nexus_build_options
    )
    add_test(NAME dual_motor_contract COMMAND dual_motor_contract)
    include(
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/rotor.cmake"
    )
    include(
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/bus.cmake"
    )
    include(
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/cia402_motor.cmake"
    )
    include(
        "${CMAKE_CURRENT_LIST_DIR}/../../../tests/apps/motor_control/single_shunt.cmake"
    )
endif()
