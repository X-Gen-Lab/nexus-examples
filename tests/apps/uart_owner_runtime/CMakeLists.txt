# Compile the maintained application sources with their host contracts.
add_executable(
    owner_lifecycle_test
    "${CMAKE_CURRENT_LIST_DIR}/owner_lifecycle_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/owner_bootstrap_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scheduler_return_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/quarantine_policy_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/lifecycle.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/bootstrap.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/scheduler_return.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/quarantine_policy.c"
)
target_include_directories(
    owner_lifecycle_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime"
)
target_compile_features(owner_lifecycle_test PRIVATE cxx_std_17)
set_target_properties(owner_lifecycle_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    owner_lifecycle_test
    PRIVATE
        Nexus::BusOwner
        Nexus::OS
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
foreach(backend IN ITEMS native baremetal freertos)
    string(TOUPPER "${backend}" backend_upper)
    add_library(
        example_policy_${backend}
        OBJECT
        "${CMAKE_CURRENT_LIST_DIR}/backend_policy.c"
    )
    target_include_directories(
        example_policy_${backend}
        PRIVATE "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime"
    )
    target_compile_definitions(
        example_policy_${backend}
        PRIVATE
            "NEXUS_BACKEND_${backend_upper}=1"
            "EXAMPLE_POLICY_SYMBOL=example_${backend}_keeps_platform"
    )
    target_link_libraries(
        example_policy_${backend}
        PRIVATE nexus_owned_warnings nexus_build_options
    )
    target_sources(
        owner_lifecycle_test
        PRIVATE "$<TARGET_OBJECTS:example_policy_${backend}>"
    )
endforeach()
add_test(
    NAME owner_lifecycle_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:owner_lifecycle_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/google-execution"
)
set_tests_properties(
    owner_lifecycle_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)

add_executable(
    product_fault_policy_test
    "${CMAKE_CURRENT_LIST_DIR}/fault_policy_test.cpp"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/fault_policy.c"
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/fault_observer.c"
)
target_include_directories(
    product_fault_policy_test
    PRIVATE "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime"
)
target_compile_features(product_fault_policy_test PRIVATE cxx_std_17)
set_target_properties(product_fault_policy_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    product_fault_policy_test
    PRIVATE
        Nexus::Core
        Nexus::Arch
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME product_fault_policy_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:product_fault_policy_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/fault-google-execution"
)
set_tests_properties(
    product_fault_policy_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
