"""Verify actual maintained application build metadata, not source patterns."""

import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from sdk_tools import ROOT, sdk_root


class ApplicationBuildContracts(unittest.TestCase):
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
                if (relative.parts[0] not in {'apps', 'reproduction'} or
                        path.name != 'main.c'):
                    continue
                target = re.search(r'CMakeFiles/([^/]+)\.dir/', item['command'])
                self.assertIsNotNone(target, item['command'])
                applications[target.group(1)] = item['command']
            self.assertEqual(set(applications), {
                'platform_reference', 'sensor_spi', 'journal', 'modbus_registers',
                'uart_owner_runtime', 'shared_spi_runtime', 'stream_recovery',
                'industrial_control', 'network_gateway'})
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
                              json.loads(catalogue.stdout)['tests']}, {
                'uart_owner_shutdown', 'platform_static_ports',
                'sensor_explicit_spi', 'persistent_journal',
                'protocol_authorized_registers', 'industrial_control_reference',
                'industrial_control_google', 'owner_lifecycle_google',
                'product_fault_policy_google', 'reference_rx_policy_google',
                'reference_lifecycle_google', 'fifo_acquisition_google',
                'usb_configuration_google',
                'shared_spi_two_devices', 'stream_slow_consumer',
                'developer_recipes_google', 'continuous_rx_google',
                'flash_maintenance_google', 'network_gateway_policy_google',
                'network_gateway_frames_google',
                'network_gateway_application_google',
                'network_gateway_peer_google', 'network_gateway_main_google'})


if __name__ == '__main__':
    unittest.main()
