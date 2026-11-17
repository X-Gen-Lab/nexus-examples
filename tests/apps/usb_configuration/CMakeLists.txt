# The application uses public USB faces; only external port facts are mocked.
get_filename_component(
    usb_examples_root
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)
add_executable(
    usb_configuration_test
    "${CMAKE_CURRENT_LIST_DIR}/usb_configuration_test.cpp"
    "${usb_examples_root}/apps/usb_configuration/usb_configuration.c"
)
target_include_directories(
    usb_configuration_test
    PRIVATE "${usb_examples_root}/apps/usb_configuration"
)
target_compile_features(usb_configuration_test PRIVATE cxx_std_17)
set_target_properties(usb_configuration_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    usb_configuration_test
    PRIVATE
        Nexus::USBControl
        Nexus::USBDescriptors
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME usb_configuration_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${usb_examples_root}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:usb_configuration_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/usb-configuration-execution"
)
set_tests_properties(
    usb_configuration_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
