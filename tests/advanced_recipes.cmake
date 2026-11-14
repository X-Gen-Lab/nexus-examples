# C11 policies are shared with firmware; only the harness links GoogleMock.
add_executable(flash_maintenance_test flash_maintenance_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/flash_maintenance/flash_application.c")
target_include_directories(flash_maintenance_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/flash_maintenance")
target_compile_features(flash_maintenance_test PRIVATE cxx_std_17)
set_target_properties(flash_maintenance_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(flash_maintenance_test PRIVATE Nexus::IO
    GTest::gmock_main nexus_owned_warnings nexus_build_options)

add_test(NAME flash_maintenance_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:flash_maintenance_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/flash_maintenance-execution")
set_tests_properties(flash_maintenance_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
