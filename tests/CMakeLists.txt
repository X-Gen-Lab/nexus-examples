# Host-only product lifecycle tests use the source SDK's pinned offline framework.
enable_language(CXX)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
file(READ "${NEXUS_SOURCE}/dependencies/googletest.lock.json" google_lock)
string(JSON google_archive GET "${google_lock}" archive)
string(JSON google_commit GET "${google_lock}" commit)
string(JSON google_sha256 GET "${google_lock}" sha256)
set(google_archive "${NEXUS_SOURCE}/${google_archive}")
if(NOT EXISTS "${google_archive}")
    message(FATAL_ERROR "Missing pinned GoogleTest archive; no network fallback")
endif()
file(SHA256 "${google_archive}" google_actual)
if(NOT google_actual STREQUAL google_sha256)
    message(FATAL_ERROR "GoogleTest archive differs from pinned source lock")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${NEXUS_SOURCE}/dependencies/googletest.lock.json" "${google_archive}")
set(google_root "${CMAKE_CURRENT_BINARY_DIR}/_deps/googletest-${google_commit}")
file(REMOVE_RECURSE "${google_root}")
file(ARCHIVE_EXTRACT INPUT "${google_archive}"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/_deps")
set(INSTALL_GTEST OFF CACHE BOOL "Host test-only framework" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "Host product lifecycle tests" FORCE)
add_subdirectory("${google_root}" google-build EXCLUDE_FROM_ALL)
add_executable(owner_lifecycle_test owner_lifecycle_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime/lifecycle.c")
target_include_directories(owner_lifecycle_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/uart_owner_runtime")
target_compile_features(owner_lifecycle_test PRIVATE cxx_std_17)
set_target_properties(owner_lifecycle_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(owner_lifecycle_test PRIVATE Nexus::BusOwner
    GTest::gmock_main nexus_owned_warnings nexus_build_options)
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
