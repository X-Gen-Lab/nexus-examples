# Select actual official class only for this explicit product reference test.
# Author: Nexus Team
add_subdirectory(
    "${PROJECT_SOURCE_DIR}/recipes/network/usb_msc"
    "${CMAKE_CURRENT_BINARY_DIR}/st-msc-reference"
)
add_executable(
    st_msc_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/st_msc_reference_test.cpp"
)
target_compile_features(st_msc_reference_test PRIVATE cxx_std_17)
set_target_properties(st_msc_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    st_msc_reference_test
    PRIVATE
        example_st_msc_reference
        Nexus::STUSBDevice
        Nexus::Platform
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME st_msc_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:st_msc_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/st-msc-reference-google-execution"
)
set_tests_properties(
    st_msc_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
