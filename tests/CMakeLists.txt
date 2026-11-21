# The application independently locks its host-only test framework.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/GoogleTest.cmake")
examples_host_google_test()

# Included groups preserve existing target and report directories while keeping
# each application's source, compiler context and failure contracts together.
include("${CMAKE_CURRENT_LIST_DIR}/apps/uart_owner_runtime/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/platform_reference/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/uart_fifo_acquisition/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/usb_configuration/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/shared_runtime/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/FreeRTOSPosix.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/apps/stream_recovery/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/flash_maintenance/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/industrial_control/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/network_gateway/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/recipes/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/apps/recipes/storage_reference.cmake")
