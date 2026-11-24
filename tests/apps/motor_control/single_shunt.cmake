# Real DC-current and integer PWM leaf contracts without phase dispatch.
# Author: Nexus Team
include(
    "${CMAKE_CURRENT_LIST_DIR}/../../../recipes/control/motor_drive/single_shunt.cmake"
)
add_executable(
    single_shunt_contract
    "${CMAKE_CURRENT_LIST_DIR}/single_shunt_test.cpp"
)
target_compile_features(single_shunt_contract PRIVATE cxx_std_17)
set_target_properties(single_shunt_contract PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    single_shunt_contract
    PRIVATE
        example_motor_drive_single
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_target_property(single_nexus_source Nexus::Config NEXUS_SOURCE_DIR)
get_filename_component(
    single_examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
add_test(
    NAME single_shunt_contract
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${single_examples_root}/scripts/run_google.py" --nexus-source
        "${single_nexus_source}" --binary "$<TARGET_FILE:single_shunt_contract>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/single-shunt-google-execution"
)
set_tests_properties(
    single_shunt_contract
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
