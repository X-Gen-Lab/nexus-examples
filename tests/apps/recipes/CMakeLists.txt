# Product recipes retain explicit execution, media and transport owners.
# Author: Nexus Team
include("${CMAKE_CURRENT_LIST_DIR}/network_reference.cmake")
add_executable(
    touch_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/touch_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/control/touch_reference.c"
)
target_include_directories(
    touch_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/control"
)
target_compile_features(touch_reference_test PRIVATE cxx_std_17)
set_target_properties(touch_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    touch_reference_test
    PRIVATE
        Nexus::XPT2046
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME touch_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:touch_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/touch-reference-google-execution"
)
set_tests_properties(
    touch_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

include("${CMAKE_CURRENT_LIST_DIR}/st_msc_reference.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/diagnostics_reference.cmake")
