# Real source protocol and qualified Task-to-fast observation handoff.
# Author: Nexus Team
include(
    "${CMAKE_CURRENT_LIST_DIR}/../../../recipes/control/motor_drive/bus.cmake"
)
add_executable(motor_bus_contract "${CMAKE_CURRENT_LIST_DIR}/bus_test.cpp")
target_compile_features(motor_bus_contract PRIVATE cxx_std_17)
set_target_properties(motor_bus_contract PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    motor_bus_contract
    PRIVATE
        example_ina226_bus
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_target_property(bus_nexus_source Nexus::Config NEXUS_SOURCE_DIR)
get_filename_component(
    bus_examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
add_test(
    NAME motor_bus_contract
    COMMAND
        "${Python3_EXECUTABLE}" -B "${bus_examples_root}/scripts/run_google.py"
        --nexus-source "${bus_nexus_source}" --binary
        "$<TARGET_FILE:motor_bus_contract>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/motor-bus-google-execution"
)
set_tests_properties(
    motor_bus_contract
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
