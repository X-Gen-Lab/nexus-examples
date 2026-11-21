# External caller owns regions, creation permission and snapshot commit policy.
# Author: Nexus Team
nexus_add_flashdb()
add_executable(
    storage_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/storage_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/storage/database_reference.c"
)
target_include_directories(
    storage_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/storage"
)
target_compile_features(storage_reference_test PRIVATE cxx_std_17)
set_target_properties(storage_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    storage_reference_test
    PRIVATE
        Nexus::FlashDB
        Nexus::Algorithms
        Nexus::Settings
        Nexus::FlashStorage
        Nexus::NativeModel
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME storage_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:storage_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/storage-reference-google-execution"
)
set_tests_properties(
    storage_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
