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

# Same C11 application behavior runs under a host-only GoogleTest harness.
add_executable(developer_recipes_test developer_recipes_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/shared_spi_application.c"
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery/stream_application.c")
target_include_directories(developer_recipes_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime"
    "${PROJECT_SOURCE_DIR}/apps/stream_recovery")
target_compile_features(developer_recipes_test PRIVATE cxx_std_17)
set_target_properties(developer_recipes_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(developer_recipes_test PRIVATE Nexus::Platform
    Nexus::SpiOwner Nexus::OSNative Nexus::Arch GTest::gmock_main
    nexus_owned_warnings nexus_build_options)
add_test(NAME developer_recipes_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:developer_recipes_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/recipes-google-execution")
set_tests_properties(developer_recipes_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")

# Reuse the pinned SDK's real POSIX FreeRTOS kernel seam. This is a test-only
# composition; Native Platform keeps its native backend and gains no RTOS port.
find_package(Threads REQUIRED)
set(NEXUS_SOURCE_DIR "${NEXUS_SOURCE}")
add_subdirectory("${NEXUS_SOURCE}/tests/contracts/os_freertos_runtime"
    freertos-kernel EXCLUDE_FROM_ALL)
add_executable(shared_spi_freertos_host
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/main.c"
    "${PROJECT_SOURCE_DIR}/apps/shared_spi_runtime/shared_spi_application.c")
target_compile_definitions(shared_spi_freertos_host PRIVATE EXAMPLE_FREERTOS_HOST=1)
target_link_libraries(shared_spi_freertos_host PRIVATE Nexus::Platform
    Nexus::SpiOwner Nexus::Arch contract_os_freertos_posix
    nexus_owned_warnings nexus_build_options)
add_dependencies(shared_spi_freertos_host contract_os_freertos)
add_test(NAME shared_spi_real_freertos COMMAND shared_spi_freertos_host)
set_tests_properties(shared_spi_real_freertos PROPERTIES TIMEOUT 30
    LABELS "native;example;FreeRTOS")

include("${CMAKE_CURRENT_LIST_DIR}/continuous_rx.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/advanced_recipes.cmake")

# The product consumer uses the same real SPI owner and POSIX wait backend.
add_executable(industrial_control_test industrial_control_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/industrial_spi.c"
    "${PROJECT_SOURCE_DIR}/apps/industrial_control/industrial_application.c")
target_include_directories(industrial_control_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/industrial_control")
target_compile_features(industrial_control_test PRIVATE cxx_std_17)
set_target_properties(industrial_control_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(industrial_control_test PRIVATE Nexus::Platform
    Nexus::SpiOwner Nexus::Settings Nexus::ModbusRTU Nexus::BMP280
    Nexus::OSNative Nexus::Arch GTest::gmock_main
    nexus_owned_warnings nexus_build_options)
target_link_options(industrial_control_test PRIVATE
    "LINKER:--wrap=nx_platform_start" "LINKER:--wrap=nx_settings_close"
    "LINKER:--wrap=nx_native_task_join")
add_test(NAME industrial_control_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:industrial_control_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/industrial-google-execution")
set_tests_properties(industrial_control_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")

include("${CMAKE_CURRENT_LIST_DIR}/network_gateway.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/network_gateway_peer.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/network_gateway_main.cmake")
