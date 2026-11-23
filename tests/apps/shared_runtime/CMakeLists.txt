# Same C11 application behavior runs under a host-only GoogleTest harness.
add_executable(
    developer_recipes_test
    "${CMAKE_CURRENT_LIST_DIR}/developer_recipes_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/shared_spi_application.c"
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery/stream_application.c"
)
target_include_directories(
    developer_recipes_test
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime"
        "${PROJECT_SOURCE_DIR}/apps/stream_recovery"
)
target_compile_features(developer_recipes_test PRIVATE cxx_std_17)
set_target_properties(developer_recipes_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    developer_recipes_test
    PRIVATE
        Nexus::Platform
        Nexus::NativeModel
        Nexus::SpiOwner
        Nexus::OS
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME developer_recipes_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:developer_recipes_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/recipes-google-execution"
)
set_tests_properties(
    developer_recipes_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

include("${CMAKE_CURRENT_LIST_DIR}/spi_bootstrap.cmake")

add_executable(
    finite_spi_test
    "${CMAKE_CURRENT_LIST_DIR}/finite_dma_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/finite_dma.c"
)
target_include_directories(
    finite_spi_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime"
)
target_compile_features(finite_spi_test PRIVATE cxx_std_17)
set_target_properties(finite_spi_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    finite_spi_test
    PRIVATE Nexus::IO GTest::gmock_main nexus_owned_warnings nexus_build_options
)
add_test(
    NAME finite_spi_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:finite_spi_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/finite-spi-execution"
)
set_tests_properties(
    finite_spi_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
