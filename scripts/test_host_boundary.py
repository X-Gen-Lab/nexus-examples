"""Exercise the installed/source-only test boundary without compiling the SDK."""

from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest

from sdk_tools import ROOT
from tdd_gate import relevant


class HostTestBoundaryTests(unittest.TestCase):
    def test_framework_gitlink_triggers_application_contract_checks(self):
        self.assertTrue(relevant('external/googletest'))

    def configure(self, explicit):
        with tempfile.TemporaryDirectory(prefix='installed host boundary ') as value:
            root = Path(value)
            sdk = root / 'installed SDK'
            sdk.mkdir()
            (sdk / '.nexus-source-sdk.json').write_text('{}\n')
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.31)\n'
                'project(host_boundary NONE)\n'
                'set(NEXUS_SOURCE "' + sdk.as_posix() + '")\n'
                'include("' + (ROOT / 'tests/cmake/FreeRTOSPosix.cmake').as_posix() + '")\n')
            args = ['cmake', '-S', str(root), '-B', str(root / 'build'), '-G', 'Ninja']
            if explicit:
                args.append('-DNEXUS_EXAMPLES_FREERTOS_POSIX_TESTS=ON')
            return subprocess.run(args, env=os.environ.copy(), capture_output=True,
                                  text=True, timeout=20)

    def test_installed_default_does_not_import_a_private_kernel_harness(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        native = next(row for row in presets["configurePresets"]
                      if row["name"] == "native-debug")
        self.assertEqual("OFF", native["cacheVariables"].get(
            "NEXUS_EXAMPLES_FREERTOS_POSIX_TESTS"))
        result = self.configure(False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_explicit_source_only_request_rejects_an_installed_sdk(self):
        result = self.configure(True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source checkout', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
