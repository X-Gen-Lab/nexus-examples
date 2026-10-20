# The pinned Nexus source checkout verifies its locked Gitlink and clean source
# before enabling host C++. Firmware never acquires the test runtime.
include("${NEXUS_SOURCE}/cmake/testing/GoogleTest.cmake")
nexus_host_google_test()
add_executable(owner_lifecycle_test owner_lifecycle_test.cpp
    owner_bootstrap_test.cpp
    scheduler_return_test.cpp
    quarantine_policy_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/lifecycle.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/bootstrap.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/scheduler_return.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/quarantine_policy.c")
target_include_directories(owner_lifecycle_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime")
target_compile_features(owner_lifecycle_test PRIVATE cxx_std_17)
set_target_properties(owner_lifecycle_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(owner_lifecycle_test PRIVATE Nexus::BusOwner
    Nexus::OSNative Nexus::Arch GTest::gmock_main nexus_owned_warnings
    nexus_build_options)
foreach(backend IN ITEMS native baremetal freertos)
    string(TOUPPER "${backend}" backend_upper)
    add_library(example_policy_${backend} OBJECT backend_policy.c)
    target_include_directories(example_policy_${backend} PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime")
    target_compile_definitions(example_policy_${backend} PRIVATE
        "NEXUS_BACKEND_${backend_upper}=1"
        "EXAMPLE_POLICY_SYMBOL=example_${backend}_keeps_platform")
    target_link_libraries(example_policy_${backend} PRIVATE
        nexus_owned_warnings nexus_build_options)
    target_sources(owner_lifecycle_test PRIVATE
        "$<TARGET_OBJECTS:example_policy_${backend}>")
endforeach()
add_test(NAME owner_lifecycle_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:owner_lifecycle_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/google-execution")
set_tests_properties(owner_lifecycle_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")

add_executable(product_fault_policy_test fault_policy_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/fault_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/fault_observer.c")
target_include_directories(product_fault_policy_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime")
target_compile_features(product_fault_policy_test PRIVATE cxx_std_17)
set_target_properties(product_fault_policy_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(product_fault_policy_test PRIVATE Nexus::Core Nexus::Arch
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
add_test(NAME product_fault_policy_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:product_fault_policy_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/fault-google-execution")
set_tests_properties(product_fault_policy_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")

add_executable(reference_rx_policy_test reference_rx_policy_test.cpp
    "${PROJECT_SOURCE_DIR}/reproduction/rx_policy.c")
target_include_directories(reference_rx_policy_test PRIVATE
    "${PROJECT_SOURCE_DIR}/reproduction")
target_compile_features(reference_rx_policy_test PRIVATE cxx_std_17)
set_target_properties(reference_rx_policy_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(reference_rx_policy_test PRIVATE Nexus::IO
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
add_test(NAME reference_rx_policy_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:reference_rx_policy_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/reference-google-execution")
set_tests_properties(reference_rx_policy_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")

add_executable(reference_lifecycle_test reference_lifecycle_test.cpp)
target_compile_features(reference_lifecycle_test PRIVATE cxx_std_17)
set_target_properties(reference_lifecycle_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(reference_lifecycle_test PRIVATE Nexus::Platform Nexus::Arch
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
add_test(NAME reference_lifecycle_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:reference_lifecycle_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/lifecycle-google-execution")
set_tests_properties(reference_lifecycle_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
include("${CMAKE_CURRENT_LIST_DIR}/uart_fifo_acquisition.cmake")
