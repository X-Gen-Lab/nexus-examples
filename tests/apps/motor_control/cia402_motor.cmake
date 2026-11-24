# Actual wire profile and motor composition, with only external ports modeled.
# Author: Nexus Team
if(NOT TARGET Nexus::CiA402)
    nexus_add_cia402()
endif()
include(
    "${CMAKE_CURRENT_LIST_DIR}/../../../recipes/network/cia402_motor/firmware.cmake"
)
add_executable(
    cia402_motor_contract
    "${CMAKE_CURRENT_LIST_DIR}/cia402_motor_test.cpp"
)
target_compile_features(cia402_motor_contract PRIVATE cxx_std_17)
set_target_properties(cia402_motor_contract PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    cia402_motor_contract
    PRIVATE
        example_cia402_motor
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_target_property(cia_motor_nexus_source Nexus::Config NEXUS_SOURCE_DIR)
get_filename_component(
    cia_motor_examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
add_test(
    NAME cia402_motor_contract
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${cia_motor_examples_root}/scripts/run_google.py" --nexus-source
        "${cia_motor_nexus_source}" --binary
        "$<TARGET_FILE:cia402_motor_contract>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/cia402-motor-google-execution"
)
set_tests_properties(
    cia402_motor_contract
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
