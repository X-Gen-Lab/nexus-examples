"""Execute the independently consumable, manifest-bound starter projects."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from sdk_tools import ROOT, sdk_root
import tdd_gate


class StarterTemplateTests(unittest.TestCase):
    def test_template_changes_enter_the_behavior_gate(self):
        self.assertTrue(tdd_gate.relevant('templates/native/main.c'))
        self.assertTrue(tdd_gate.relevant('templates/manifest.json'))


    def test_manifest_binds_every_owned_template_file(self):
        manifest = json.loads((ROOT / 'templates/manifest.json').read_text())
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(manifest['version'], 1)
        self.assertEqual(set(manifest['templates']),
                         {'native', 'qiming', 'sky', 'liangshan'})
        for name, entry in manifest['templates'].items():
            with self.subTest(template=name):
                directory = ROOT / 'templates' / name
                actual = {str(path.relative_to(directory)):
                          hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in directory.rglob('*') if path.is_file()}
                self.assertEqual(entry['files'], actual)
                self.assertEqual(set(actual),
                                 {'CMakeLists.txt', 'assembly.toml',
                                  'main.c', 'README.md'})
                self.assertEqual(entry['platform'],
                                 {'interface_epoch': 1, 'assembly_schema': 2})

    def test_every_template_resolves_independently(self):
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix='starter configuration ') as value:
            directory = Path(value)
            for name in ('native', 'qiming', 'sky', 'liangshan'):
                with self.subTest(template=name):
                    assembly = ROOT / 'templates' / name / 'assembly.toml'
                    result = subprocess.run(
                        ['python3', '-B', str(source / 'tools/configure/configure.py'),
                         '--assembly', str(assembly), '--output', str(directory / name)],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    ir = json.loads((directory / name / 'resolved.json').read_text())
                    self.assertEqual({item['id'] for item in ir['controllers']},
                                     {'console', 'status'})

    def test_relocated_native_starter_builds_and_runs(self):
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix='independent starter ') as value:
            directory = Path(value)
            project = directory / 'owned product'
            shutil.copytree(ROOT / 'templates/native', project)
            build = directory / 'build'
            commands = [
                ['cmake', '-S', str(project), '-B', str(build), '-G', 'Ninja',
                 '-DNEXUS_SOURCE:PATH=' + str(source), '-DCMAKE_BUILD_TYPE=Debug'],
                ['cmake', '--build', str(build), '--parallel', '1'],
                ['ctest', '--test-dir', str(build), '--output-on-failure',
                 '--no-tests=error'],
            ]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True,
                                        timeout=180)
                self.assertEqual(result.returncode, 0,
                                 ' '.join(command) + '\n' + result.stdout + result.stderr)
            result = subprocess.run([str(build / 'bin/nexus_starter')],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Nexus starter: transfer settled; platform stopped',
                          result.stdout)


if __name__ == '__main__':
    unittest.main()
