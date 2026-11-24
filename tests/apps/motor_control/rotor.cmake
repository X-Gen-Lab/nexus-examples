# Execute the actual reader and bounded exchange against a wire-only sensor.
# Author: Nexus Team
include(
    "${CMAKE_CURRENT_LIST_DIR}/../../../recipes/control/motor_drive/rotor.cmake"
)
add_executable(motor_rotor_contract "${CMAKE_CURRENT_LIST_DIR}/rotor_test.cpp")
target_compile_features(motor_rotor_contract PRIVATE cxx_std_17)
set_target_properties(motor_rotor_contract PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    motor_rotor_contract
    PRIVATE
        example_as5047p_rotor
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_target_property(rotor_nexus_source Nexus::Config NEXUS_SOURCE_DIR)
get_filename_component(
    rotor_examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
add_test(
    NAME motor_rotor_contract
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${rotor_examples_root}/scripts/run_google.py" --nexus-source
        "${rotor_nexus_source}" --binary "$<TARGET_FILE:motor_rotor_contract>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/motor-rotor-google-execution"
)
set_tests_properties(
    motor_rotor_contract
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
