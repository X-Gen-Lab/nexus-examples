# The application owns its host framework; the firmware SDK exports no tests.
include_guard(GLOBAL)
get_filename_component(examples_google_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

macro(examples_host_google_test)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Project GoogleTest is host-only")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -B
        "${examples_google_root}/scripts/googletest_lock.py"
        RESULT_VARIABLE examples_google_result OUTPUT_VARIABLE examples_google_identity
        ERROR_VARIABLE examples_google_error)
    if(NOT examples_google_result EQUAL 0)
        message(FATAL_ERROR "${examples_google_error}")
    endif()
    enable_language(CXX)
    string(JSON examples_google_count LENGTH "${examples_google_identity}" configure_inputs)
    math(EXPR examples_google_last "${examples_google_count} - 1")
    foreach(examples_google_index RANGE 0 ${examples_google_last})
        string(JSON examples_google_input GET "${examples_google_identity}"
            configure_inputs ${examples_google_index})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${examples_google_input}")
    endforeach()
    string(JSON examples_google_count LENGTH "${examples_google_identity}" configure_file_probes)
    math(EXPR examples_google_last "${examples_google_count} - 1")
    foreach(examples_google_index RANGE 0 ${examples_google_last})
        string(JSON examples_google_input GET "${examples_google_identity}"
            configure_file_probes ${examples_google_index})
        file(GLOB examples_google_present LIST_DIRECTORIES false CONFIGURE_DEPENDS
            "${examples_google_input}")
    endforeach()
    set(INSTALL_GTEST OFF CACHE BOOL "Project host tests only" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "Project GoogleMock contracts" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "Host MSVC runtime" FORCE)
    add_subdirectory("${examples_google_root}/external/googletest"
        "${CMAKE_BINARY_DIR}/_deps/examples-google" EXCLUDE_FROM_ALL)
endmacro()
