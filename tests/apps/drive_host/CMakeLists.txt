# Exercise the real standard-library host application with an external CAN port.
# Author: Nexus Team
add_test(
    NAME drive_host_reference
    COMMAND
        "${Python3_EXECUTABLE}" -m unittest discover -s
        "${PROJECT_SOURCE_DIR}/tests/apps/drive_host" -p test_drive_host.py -v
)
set_tests_properties(
    drive_host_reference
    PROPERTIES TIMEOUT 10 LABELS "native;example;host"
)
