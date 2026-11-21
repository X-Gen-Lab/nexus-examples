"""Verify actual maintained application build metadata, not source patterns."""

import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from sdk_tools import ROOT, sdk_root
from tdd_gate import EXPECTED_CTEST_NAMES


class ApplicationBuildContracts(unittest.TestCase):
    def test_arm_preset_configures_without_a_source_environment_variable(self):
        environment = os.environ.copy()
        environment.pop('NEXUS_SOURCE', None)
        with tempfile.TemporaryDirectory(prefix='direct ARM preset ') as value:
            result = subprocess.run([
                'cmake', '--preset', 'stm32-qiming-baremetal', '-B', value],
                cwd=ROOT, env=environment, capture_output=True, text=True,
                timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (Path(value) / 'CMakeCache.txt').read_text()
            system = next((Path(value) / 'CMakeFiles')
                          .glob('*/CMakeSystem.cmake')).read_text()
            self.assertIn('CMAKE_SYSTEM_NAME "Generic"', system)
            self.assertIn(str(ROOT / 'external/nexus'), cache)

    def test_conflicting_sdk_sources_reject_before_compiler_detection(self):
        environment = dict(os.environ, NEXUS_SOURCE=str(sdk_root()))
        with tempfile.TemporaryDirectory(prefix='conflicting SDK ') as value:
            result = subprocess.run([
                'cmake', '-S', str(ROOT), '-B', value, '-G', 'Ninja',
                '-DNEXUS_SOURCE:PATH=' + str(Path(value) / 'other-sdk')],
                cwd=ROOT, env=environment, capture_output=True, text=True,
                timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('NEXUS_SOURCE differs', result.stderr)
            self.assertNotIn('compiler identification', result.stdout)

    def test_native_exports_all_applications_with_sanitizer_instrumentation(self):
        environment = os.environ.copy()
        environment['NEXUS_SOURCE'] = str(sdk_root())
        with tempfile.TemporaryDirectory(prefix='example compile metadata ') as value:
            directory = Path(value)
            result = subprocess.run(
                ['cmake', '--preset', 'native-debug', '-B', str(directory),
                 '-DNEXUS_SOURCE:PATH=' + str(sdk_root())],
                cwd=ROOT, env=environment, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            commands = json.loads((directory / 'compile_commands.json').read_text())
            applications = {}
            for item in commands:
                path = Path(item['file'])
                if not path.is_relative_to(ROOT):
                    continue
                relative = path.relative_to(ROOT)
                if (relative.parts[0] not in {'apps'} or
                        path.name != 'main.c'):
                    continue
                target = re.search(r'CMakeFiles/([^/]+)\.dir/', item['command'])
                self.assertIsNotNone(target, item['command'])
                applications[target.group(1)] = item['command']
            self.assertEqual(set(applications), {
                'platform_reference', 'sensor_spi', 'journal', 'modbus_registers',
                'uart_owner_runtime', 'shared_spi_runtime', 'stream_recovery',
                'industrial_control', 'network_gateway',
                'reference_bootstrap_main', 'uart_bootstrap_main'})
            application_targets = set(applications)
            for relative, targets in (
                    ('apps/flash_maintenance/flash_application.c',
                     {'flash_maintenance_test'}),
                    ('apps/usb_configuration/usb_configuration.c',
                     {'usb_configuration_test'})):
                selected = [item['command'] for item in commands
                            if Path(item['file']) == ROOT / relative]
                self.assertEqual(len(selected), len(targets), relative)
                actual = {}
                for command in selected:
                    target = re.search(r'CMakeFiles/([^/]+)\.dir/', command)
                    self.assertIsNotNone(target, command)
                    actual[target.group(1)] = command
                self.assertEqual(set(actual), targets)
                # The exact production C policy is instrumented independently
                # from its host-only C++ GoogleTest/GoogleMock harness.
                applications.update(actual)
            for name, command in applications.items():
                with self.subTest(application=name):
                    self.assertIn('-std=c11', command.split())
                    self.assertTrue(any('-fsanitize=' in flag and 'address' in flag
                                        for flag in command.split()))
                    self.assertTrue(any('-fsanitize=' in flag and 'undefined' in flag
                                        for flag in command.split()))
                    if name in application_targets:
                        self.assertNotIn('ext/googletest', command)
            framework = {
                Path(item['file']).name for item in commands
                if Path(item['file']).is_relative_to(
                    ROOT / 'external/googletest')}
            self.assertEqual(framework, {
                'gtest-all.cc', 'gtest_main.cc', 'gmock-all.cc', 'gmock_main.cc'})
            catalogue = subprocess.run(
                ['ctest', '--test-dir', str(directory), '--show-only=json-v1'],
                cwd=ROOT, env=environment, capture_output=True, text=True,
                timeout=30)
            self.assertEqual(catalogue.returncode, 0, catalogue.stderr)
            self.assertEqual({test['name'] for test in
                              json.loads(catalogue.stdout)['tests']},
                             set(EXPECTED_CTEST_NAMES))


if __name__ == '__main__':
    unittest.main()
