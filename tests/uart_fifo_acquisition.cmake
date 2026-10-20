# Include from the existing examples Native tests after its pinned GTest setup.
add_executable(uart_fifo_acquisition_test uart_fifo_acquisition_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/uart_fifo_acquisition/workload.c")
target_include_directories(uart_fifo_acquisition_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/uart_fifo_acquisition")
target_compile_features(uart_fifo_acquisition_test PRIVATE cxx_std_17)
set_target_properties(uart_fifo_acquisition_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(uart_fifo_acquisition_test PRIVATE Nexus::IO
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
add_test(NAME fifo_acquisition_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:uart_fifo_acquisition_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/fifo-acquisition-execution")
set_tests_properties(fifo_acquisition_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
