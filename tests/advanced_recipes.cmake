# C11 policies are shared with firmware; only the harness links GoogleMock.
add_executable(flash_maintenance_test flash_maintenance_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/flash_maintenance/flash_application.c")
target_include_directories(flash_maintenance_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/flash_maintenance")
target_compile_features(flash_maintenance_test PRIVATE cxx_std_17)
set_target_properties(flash_maintenance_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(flash_maintenance_test PRIVATE Nexus::IO
    GTest::gmock_main nexus_owned_warnings nexus_build_options)

add_executable(retained_stop2_test retained_stop2_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/retained_stop2/stop2_application.c")
target_include_directories(retained_stop2_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/retained_stop2"
    "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_stop2/include"
    "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_retained_time/include")
target_compile_features(retained_stop2_test PRIVATE cxx_std_17)
set_target_properties(retained_stop2_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(retained_stop2_test PRIVATE Nexus::Platform
    Nexus::ClockDomain Nexus::Arch GTest::gmock_main nexus_owned_warnings
    nexus_build_options)

foreach(recipe IN ITEMS flash_maintenance retained_stop2)
    add_test(NAME ${recipe}_google COMMAND "${Python3_EXECUTABLE}" -B
        "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}"
        --binary "$<TARGET_FILE:${recipe}_test>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/${recipe}-execution")
    set_tests_properties(${recipe}_google PROPERTIES TIMEOUT 30
        LABELS "native;example;GoogleTest")
endforeach()

add_executable(compound_stop2_test compound_stop2_test.cpp
    "${PROJECT_SOURCE_DIR}/apps/retained_stop2/stop2_application.c")
target_compile_definitions(compound_stop2_test PRIVATE EXAMPLE_STOP2_COMPOUND=1)
target_include_directories(compound_stop2_test PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/retained_stop2"
    "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_stop2/include"
    "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_retained_time/include")
target_compile_features(compound_stop2_test PRIVATE cxx_std_17)
set_target_properties(compound_stop2_test PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(compound_stop2_test PRIVATE Nexus::Platform
    Nexus::ClockDomain Nexus::Arch GTest::gmock_main nexus_owned_warnings
    nexus_build_options)
add_test(NAME compound_stop2_google COMMAND "${Python3_EXECUTABLE}" -B
    "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
    --nexus-source "${NEXUS_SOURCE}"
    --binary "$<TARGET_FILE:compound_stop2_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/compound-stop2-execution")
set_tests_properties(compound_stop2_google PROPERTIES TIMEOUT 30
    LABELS "native;example;GoogleTest")
include("${CMAKE_CURRENT_LIST_DIR}/lowpower_recipe_hooks.cmake")
