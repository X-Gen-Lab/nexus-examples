# Compile the actual C11 firmware hooks; GC removes the renamed firmware entry.
foreach(mode IN ITEMS copy lptim1)
    add_library(stop2_${mode}_hook_source OBJECT
        "${PROJECT_SOURCE_DIR}/apps/retained_stop2/u575_${mode}/main.c")
    target_include_directories(stop2_${mode}_hook_source PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/retained_stop2"
        "${NEXUS_SOURCE}/core/include"
        "${NEXUS_SOURCE}/arch/include"
        "${NEXUS_SOURCE}/io/include"
        "${NEXUS_SOURCE}/components/clock_domain/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_stop2/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_retained_time/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_lpdma_copy/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_lpdma_lptim1/include")
    target_compile_definitions(stop2_${mode}_hook_source PRIVATE
        EXAMPLE_STOP2_COMPOUND=1 "main=example_stop2_${mode}_firmware_main")
    target_compile_options(stop2_${mode}_hook_source PRIVATE
        -ffunction-sections -fdata-sections
        -include "${CMAKE_CURRENT_LIST_DIR}/lowpower_recipe_host_config.h")
    target_link_libraries(stop2_${mode}_hook_source PRIVATE Nexus::Config
        nexus_owned_warnings nexus_build_options)

    add_executable(stop2_${mode}_hooks_test lowpower_recipe_hooks_test.cpp
        "$<TARGET_OBJECTS:stop2_${mode}_hook_source>")
    target_include_directories(stop2_${mode}_hooks_test PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/retained_stop2"
        "${NEXUS_SOURCE}/core/include"
        "${NEXUS_SOURCE}/arch/include"
        "${NEXUS_SOURCE}/io/include"
        "${NEXUS_SOURCE}/components/clock_domain/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_stop2/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_retained_time/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_lpdma_copy/include"
        "${NEXUS_SOURCE}/components/soc/stm32/stm32u575_lpdma_lptim1/include")
    target_compile_definitions(stop2_${mode}_hooks_test PRIVATE
        EXAMPLE_STOP2_COMPOUND=1)
    if(mode STREQUAL "copy")
        target_compile_definitions(stop2_${mode}_hooks_test PRIVATE
            EXAMPLE_STOP2_COPY_HOOK_TEST=1)
    endif()
    target_compile_features(stop2_${mode}_hooks_test PRIVATE cxx_std_17)
    set_target_properties(stop2_${mode}_hooks_test PROPERTIES CXX_EXTENSIONS OFF)
    target_link_options(stop2_${mode}_hooks_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(stop2_${mode}_hooks_test PRIVATE Nexus::Config
        GTest::gmock_main nexus_owned_warnings
        nexus_build_options)
    add_test(NAME stop2_${mode}_hooks_google COMMAND "${Python3_EXECUTABLE}" -B
        "${PROJECT_SOURCE_DIR}/scripts/run_google.py"
        --nexus-source "${NEXUS_SOURCE}"
        --binary "$<TARGET_FILE:stop2_${mode}_hooks_test>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/stop2-${mode}-hooks-execution")
    set_tests_properties(stop2_${mode}_hooks_google PROPERTIES TIMEOUT 30
        LABELS "native;example;GoogleTest")
endforeach()
