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
                'uart_owner_runtime'})
            for name, command in applications.items():
                with self.subTest(application=name):
                    self.assertTrue(any('-fsanitize=' in flag and 'address' in flag
                                        for flag in command.split()))
                    self.assertTrue(any('-fsanitize=' in flag and 'undefined' in flag
                                        for flag in command.split()))


if __name__ == '__main__':
    unittest.main()
