# Fixed model composition uses only the public inference adapter.
# Author: Nexus Team
nexus_add_cmsis_nn()
add_executable(
    inference_reference_test
    "${CMAKE_CURRENT_LIST_DIR}/inference_reference_test.cpp"
    "${PROJECT_SOURCE_DIR}/recipes/inference/inference_reference.c"
)
target_include_directories(
    inference_reference_test
    PRIVATE "${PROJECT_SOURCE_DIR}/recipes/inference"
)
target_compile_features(inference_reference_test PRIVATE cxx_std_17)
set_target_properties(inference_reference_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    inference_reference_test
    PRIVATE
        Nexus::AlgorithmsCMSISNN
        GTest::gmock_main
        nexus_owned_warnings
        nexus_build_options
)
add_test(
    NAME inference_reference_google
    COMMAND
        "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}" --binary
        "$<TARGET_FILE:inference_reference_test>" --output
        "${CMAKE_CURRENT_BINARY_DIR}/inference-reference-google-execution"
)
set_tests_properties(
    inference_reference_google
    PROPERTIES TIMEOUT 30 LABELS "native;example;GoogleTest"
)
