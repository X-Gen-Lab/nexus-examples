"""Execute the independently consumable, manifest-bound starter projects."""

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib
import unittest

from sdk_tools import ROOT, sdk_root
import tdd_gate


class StarterTemplateTests(unittest.TestCase):
    def test_template_changes_enter_the_behavior_gate(self):
        self.assertTrue(tdd_gate.relevant('templates/native/main.c'))
        self.assertTrue(tdd_gate.relevant('templates/manifest.json'))
        self.assertTrue(tdd_gate.relevant('.taplo.toml'))

    def test_copied_starters_keep_exact_schema_association(self):
        lock = json.loads((ROOT / 'dependencies/nexus.lock.json').read_text())
        schema = ('https://raw.githubusercontent.com/X-Gen-Lab/nexus/'
                  + lock['commit'] + '/tools/configure/assembly.schema.json')
        manifest = json.loads((ROOT / 'templates/manifest.json').read_text())
        config = ROOT / '.taplo.toml'
        self.assertTrue(config.is_file(), 'Checkout needs assembly associations')
        settings = tomllib.loads(config.read_text())
        self.assertEqual(settings, {
            'include': ['configs/*.toml', 'templates/*/assembly.toml',
                        'apps/*/assembly.toml'],
            'schema': {'path': schema}})
        with tempfile.TemporaryDirectory(prefix='editor copied product ') as value:
            for name in manifest['templates']:
                with self.subTest(template=name):
                    project = Path(value) / name / 'owned product'
                    shutil.copytree(ROOT / 'templates' / name, project)
                    config = project / '.taplo.toml'
                    self.assertTrue(config.is_file(),
                                    'Copied starter needs its schema association')
                    settings = tomllib.loads(config.read_text())
                    self.assertEqual(settings['include'], ['assembly.toml'])
                    self.assertEqual(settings['schema'], {'path': schema})
                    self.assertEqual(set(settings), {'include', 'schema'})


    def test_manifest_binds_every_owned_template_file(self):
        manifest = json.loads((ROOT / 'templates/manifest.json').read_text())
        policy = json.loads((sdk_root() / 'dependencies/platform-compatibility.json')
                            .read_text())
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(manifest['version'], 1)
        self.assertTrue({'native', 'qiming', 'sky', 'qiming-core', 'qiming-v41'}.issubset(
            manifest['templates']))
        directories = {path.name for path in (ROOT / 'templates').iterdir()
                       if path.is_dir()}
        self.assertEqual(set(manifest['templates']), directories)
        for name, entry in manifest['templates'].items():
            with self.subTest(template=name):
                self.assertEqual(set(entry), {'files', 'platform'})
                directory = ROOT / 'templates' / name
                actual = {str(path.relative_to(directory)):
                          hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in directory.rglob('*') if path.is_file()}
                self.assertEqual(entry['files'], actual)
                self.assertEqual(set(actual),
                                 {'CMakeLists.txt', 'assembly.toml',
                                  'main.c', 'README.md', '.taplo.toml'})
                self.assertEqual(entry['platform'],
                                 {'interface_epoch': policy['factory_abi_epoch'],
                                  'assembly_schema': 2})

    def test_current_templates_resolve_independently(self):
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix='starter configuration ') as value:
            directory = Path(value)
            manifest = json.loads((ROOT / 'templates/manifest.json').read_text())
            policy = json.loads((source / 'dependencies/platform-compatibility.json')
                                .read_text())
            expected = {'interface_epoch': policy['factory_abi_epoch'],
                        'assembly_schema': policy['schemas']['assembly_authoring']}
            for name, entry in manifest['templates'].items():
                self.assertEqual(entry['platform'], expected)
                with self.subTest(template=name):
                    assembly = ROOT / 'templates' / name / 'assembly.toml'
                    command = ['python3', '-B', str(source / 'tools/configure/configure.py'),
                               '--assembly', str(assembly), '--output', str(directory / name)]
                    if name == 'native':
                        command.append('--test-support')
                    result = subprocess.run(command,
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    ir = json.loads((directory / name / 'resolved.json').read_text())
                    self.assertEqual({item['id'] for item in ir['controllers']},
                                     {'console', 'status'})

    def test_schematic_qiming_templates_have_explicit_composition(self):
        manifest = json.loads((ROOT / 'templates/manifest.json').read_text())
        expected = {
            'qiming-core': ('stm32f407zg_qiming_core', 'PG13'),
            'qiming-v41': ('stm32f407zg_qiming_v41', 'PE3'),
        }
        source = sdk_root()
        for name, (board, _) in expected.items():
            with self.subTest(template=name):
                self.assertIn(name, manifest['templates'])
                directory = ROOT / 'templates' / name
                assembly = tomllib.loads((directory / 'assembly.toml').read_text())
                self.assertEqual(assembly['board'], board)
                self.assertEqual(assembly['backend'], 'baremetal')
                self.assertEqual(assembly['clock'], 'hse8-pll168')
                facts = json.loads((source / 'boards' / board / 'board.json').read_text())
                selections = assembly['hardware']
                self.assertEqual(set(selections), set(facts['hardware_options']))
                for option, choice in selections.items():
                    self.assertIn(choice, facts['hardware_options'][option])
                    self.assertEqual(choice, 'swd' if option == 'debug_port'
                                     else 'absent' if 'absent' in
                                     facts['hardware_options'][option]
                                     else 'disconnected')
                self.assertEqual(set(assembly['gpio']), {'status'})
                self.assertEqual(set(assembly['uart']), {'console'})
                self.assertEqual(assembly['gpio']['status']['binding'], 'led0')
                self.assertEqual(assembly['uart']['console']['binding'], 'uart1')
                self.assertEqual(set(assembly), {
                    'schema', 'board', 'backend', 'clock', 'optimization',
                    'hardware', 'memory', 'gpio', 'uart'})
                # Reuse the established public-only application unchanged.
                for owned in ('CMakeLists.txt', 'main.c'):
                    self.assertEqual((directory / owned).read_bytes(),
                                     (ROOT / 'templates/qiming' / owned).read_bytes())
                includes = set(re.findall(r'^#include [<"]([^>"]+)[>"]',
                                          (directory / 'main.c').read_text(),
                                          re.MULTILINE))
                self.assertEqual(includes, {
                    'nexus/io/gpio.h', 'nexus/io/uart.h', 'nexus_bindings.h',
                    'nexus_config.h', 'nexus_factory_strong.h', 'stdio.h'})

    def test_schematic_qiming_templates_resolve_exact_pins_without_modules(self):
        source = sdk_root()
        expected = {
            'qiming-core': ('stm32f407zg-qiming-core', 'PG13', 8192),
            'qiming-v41': ('stm32f407zg-qiming-v41', 'PE3', 8),
        }
        with tempfile.TemporaryDirectory(prefix='schematic qiming ') as value:
            for name, (board, pin, initial) in expected.items():
                with self.subTest(template=name):
                    assembly = ROOT / 'templates' / name / 'assembly.toml'
                    output = Path(value) / name
                    result = subprocess.run(
                        ['python3', '-B', str(source / 'tools/configure/configure.py'),
                         '--assembly', str(assembly), '--output', str(output)],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0,
                                     result.stdout + result.stderr)
                    ir = json.loads((output / 'resolved.json').read_text())
                    self.assertEqual(ir['board'], board)
                    controllers = {item['id']: item for item in ir['controllers']}
                    self.assertEqual(set(controllers), {'console', 'status'})
                    self.assertEqual(controllers['status']['pins'][0]['pin'], pin)
                    self.assertEqual(controllers['status']['initial'], initial)
                    self.assertEqual(controllers['console']['route'],
                                     'usart1-pa9-pa10')
                    self.assertEqual(ir['hardware']['selected'],
                                     tomllib.loads(assembly.read_text())['hardware'])
                    self.assertFalse(ir['physical_qualified'])
                    self.assertEqual(ir['hardware']['physical_verification'],
                                     'not_established_by_configuration')
                    config = (output / 'nexus_config.h').read_text()
                    factory = (output / 'nexus_factory_strong.h').read_text()
                    self.assertIn('NEXUS_STATUS_MASK', config)
                    self.assertIn('NX_GPIO_REF_STATUS', factory)
                    self.assertIn('NX_UART_REF_CONSOLE', factory)

    def test_relocated_native_starter_builds_and_runs(self):
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix='independent starter ') as value:
            directory = Path(value)
            project = directory / 'owned product'
            shutil.copytree(ROOT / 'templates/native', project)
            build = directory / 'build'
            commands = [
                ['cmake', '-S', str(project), '-B', str(build), '-G', 'Ninja',
                 '-DNexus_DIR:PATH=' + str(source / 'cmake'), '-DCMAKE_BUILD_TYPE=Debug'],
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

    def test_native_starter_explicitly_admits_the_host_model(self):
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix='native starter admission ') as value:
            build = Path(value) / 'build'
            result = subprocess.run([
                'cmake', '-S', str(ROOT / 'templates/native'), '-B', str(build),
                '-G', 'Ninja', '-DNexus_DIR:PATH=' + str(source / 'cmake'),
                '-DCMAKE_BUILD_TYPE=Debug'], capture_output=True, text=True,
                timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('NEXUS_TEST_SUPPORT:BOOL=ON',
                          (build / 'CMakeCache.txt').read_text())


if __name__ == '__main__':
    unittest.main()
