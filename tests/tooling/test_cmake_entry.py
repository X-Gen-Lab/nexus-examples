"""Exercise the examples' explicit assembly handoff to the public SDK API."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from sdk_tools import ROOT


class ExplicitPlatformEntryTests(unittest.TestCase):
    def test_motor_observation_layouts_cannot_share_one_consumer(self):
        with tempfile.TemporaryDirectory(prefix="motor layout consumer ") as value:
            directory = Path(value)
            leaf = ROOT / "recipes/control/motor_drive"
            (directory / "main.c").write_text("int main(void) { return 0; }\n")
            (directory / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.31)\n'
                'project(motor_layout_contract LANGUAGES C)\n'
                'foreach(name Core Arch DriveControl CurrentControl ModulationControl '
                'IOPowerStage Algorithms AS5047P INA226)\n'
                '  add_library(Nexus::${name} INTERFACE IMPORTED)\n'
                'endforeach()\n'
                'add_library(nexus_owned_warnings INTERFACE)\n'
                'add_library(nexus_build_options INTERFACE)\n'
                'function(nexus_apply_target_policy target)\n'
                'endfunction()\n'
                f'include("{leaf.as_posix()}/firmware.cmake")\n'
                f'include("{leaf.as_posix()}/single_shunt.cmake")\n'
                'add_executable(consumer main.c)\n'
                'if(SELECTED STREQUAL "phase")\n'
                '  target_link_libraries(consumer PRIVATE example_motor_drive)\n'
                'elseif(SELECTED STREQUAL "single")\n'
                '  target_link_libraries(consumer PRIVATE example_motor_drive_single)\n'
                'else()\n'
                '  target_link_libraries(consumer PRIVATE example_motor_drive '
                'example_motor_drive_single)\n'
                'endif()\n')
            for selected in ("phase", "single", "both"):
                with self.subTest(selected=selected):
                    result = subprocess.run([
                        "cmake", "-S", str(directory), "-B",
                        str(directory / selected), "-G", "Ninja",
                        "-DSELECTED=" + selected], capture_output=True,
                        text=True, timeout=30)
                    if selected == "both":
                        self.assertNotEqual(result.returncode, 0,
                                            result.stdout + result.stderr)
                        self.assertIn("EXAMPLE_MOTOR_CURRENT_PATH", result.stderr)
                    else:
                        self.assertEqual(result.returncode, 0,
                                         result.stdout + result.stderr)

    def test_every_consumer_creates_its_assembly_before_adding_images(self):
        with tempfile.TemporaryDirectory(prefix="example explicit assembly ") as value:
            directory = Path(value)
            sdk = directory / "source SDK"
            package = sdk / "cmake"
            package.mkdir(parents=True)
            (sdk / "CMakeLists.txt").write_text(
                'message(FATAL_ERROR "The consumer entered the SDK developer project")\n')
            (package / "NexusConfigVersion.cmake").write_text(
                'set(PACKAGE_VERSION "0.2.1")\n'
                'set(PACKAGE_VERSION_COMPATIBLE TRUE)\n'
                'set(PACKAGE_VERSION_EXACT TRUE)\n')
            (package / "NexusConfig.cmake").write_text(
                'get_filename_component(Nexus_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)\n'
                'function(nexus_add_platform)\n'
                '  cmake_parse_arguments(PLATFORM "" "ASSEMBLY" "" ${ARGN})\n'
                '  if(NOT EXISTS "${PLATFORM_ASSEMBLY}")\n'
                '    message(FATAL_ERROR "Explicit assembly is missing")\n'
                '  endif()\n'
                '  if(DEFINED NEXUS_BUILD_TESTS OR DEFINED NEXUS_BUILD_WORKLOAD)\n'
                '    message(FATAL_ERROR "Consumer set SDK developer switches")\n'
                '  endif()\n'
                '  add_library(example_platform INTERFACE)\n'
                '  add_library(Nexus::Platform ALIAS example_platform)\n'
                '  set_property(TARGET example_platform PROPERTY NEXUS_SOC_FAMILY probe)\n'
                '  file(WRITE "${CMAKE_BINARY_DIR}/assembly.txt" "${PLATFORM_ASSEMBLY}")\n'
                'endfunction()\n'
                'function(nexus_add_firmware target)\n'
                '  if(NOT TARGET Nexus::Platform)\n'
                '    message(FATAL_ERROR "Assembly must be explicitly created first")\n'
                '  endif()\n'
                '  add_library(${target} INTERFACE)\n'
                'endfunction()\n')
            environment = os.environ.copy()
            environment.pop("NEXUS_SOURCE", None)
            expected = {
                ".": ROOT / "configs/qiming-baremetal.toml",
                **{"templates/" + name: ROOT / "templates" / name / "assembly.toml"
                   for name in ("native", "qiming", "sky", "qiming-core", "qiming-v41")},
                "tests/integration/installed_sdk": ROOT / "configs/native.toml",
            }
            for index, (relative, assembly) in enumerate(expected.items()):
                with self.subTest(consumer=relative):
                    build = directory / str(index)
                    arguments = [
                        "cmake", "-S", str(ROOT / relative), "-B", str(build),
                        "-G", "Ninja", "-DNexus_DIR=" + str(package)]
                    if relative == ".":
                        arguments.append("-DNEXUS_SOURCE=" + str(sdk))
                    if relative in (".", "tests/integration/installed_sdk"):
                        arguments.append("-DNEXUS_ASSEMBLY_FILE=" + str(assembly))
                    result = subprocess.run(arguments, env=environment,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual((build / "assembly.txt").read_text(), str(assembly))


if __name__ == "__main__":
    unittest.main()
