"""Keep real example execution and every firmware budget in CI evidence."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml

from sdk_tools import sdk_root


ROOT = Path(__file__).resolve().parents[1]


class ExampleWorkflow(unittest.TestCase):
    def test_host_preparation_bounds_downloads_and_stops_on_dependency_errors(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        # Only external commands are replaced; execute each actual CI script.
        command_stub = f'#!{sys.executable}\n' + '''
import json
import os
from pathlib import Path
import sys

command = Path(sys.argv[0]).name
arguments = sys.argv[1:]
with open(os.environ['PREPARATION_LOG'], 'a') as stream:
    stream.write(json.dumps([command, *arguments]) + '\\n')
if command == 'apt-get':
    if not any(operation in arguments for operation in ('update', 'install')):
        raise SystemExit(127)
    if 'update' in arguments and os.environ['PREPARATION_FAILURE'] == 'index':
        print('W: Some index files failed to download', file=sys.stderr)
        raise SystemExit(100 if 'APT::Update::Error-Mode=any' in arguments else 0)
    if 'install' in arguments and os.environ['PREPARATION_FAILURE'] == 'install':
        raise SystemExit(100)
elif command == 'python3':
    if arguments != ['scripts/setup_dev_tools.py', '--skip-hooks']:
        raise SystemExit(127)
elif command == 'python':
    if arguments[:3] != ['-m', 'pip', 'install']:
        raise SystemExit(127)
else:
    raise SystemExit(127)
'''
        for job in ('native', 'arm'):
            step = next(step for step in document['jobs'][job]['steps']
                        if step.get('name') == 'Prepare host build dependencies')
            for failure in ('index', 'install', 'none'):
                with self.subTest(job=job, failure=failure), \
                        tempfile.TemporaryDirectory(prefix='host preparation ') as temporary:
                    root = Path(temporary)
                    tools = root / 'bin'
                    tools.mkdir()
                    python = root / '.venv/bin/python'
                    python.parent.mkdir(parents=True)
                    for path in (tools / 'apt-get', tools / 'python3', python):
                        path.write_text(command_stub)
                        path.chmod(0o755)
                    sudo = tools / 'sudo'
                    sudo.write_text('#!/bin/bash\n'
                                    '[[ "$1" == apt-get ]] || exit 127\n'
                                    'exec "$@"\n')
                    sudo.chmod(0o755)
                    log, published = root / 'commands.jsonl', root / 'github-path'
                    environment = dict(os.environ, PREPARATION_LOG=str(log),
                                       PREPARATION_FAILURE=failure,
                                       GITHUB_WORKSPACE=str(root),
                                       GITHUB_PATH=str(published))
                    environment['PATH'] = str(tools) + os.pathsep + environment['PATH']
                    result = subprocess.run(
                        ['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail',
                         '-c', step['run']], cwd=root, env=environment,
                        text=True, capture_output=True, timeout=10)
                    calls = [json.loads(line) for line in log.read_text().splitlines()]
                    self.assertEqual(result.returncode, 0 if failure == 'none' else 100,
                                     result.stdout + result.stderr + repr(calls))
                    if failure != 'none':
                        self.assertEqual(len(calls), 1 if failure == 'index' else 2)
                        self.assertTrue(all(call[0] == 'apt-get' for call in calls))
                        self.assertFalse(published.exists())
                        continue
                    self.assertEqual([call[0] for call in calls],
                                     ['apt-get', 'apt-get', 'python3', 'python'])
                    self.assertIn('update', calls[0])
                    self.assertIn('install', calls[1])
                    expected = {'Acquire::http::Timeout': '30',
                                'Acquire::https::Timeout': '30',
                                'Acquire::Retries': '3'}
                    for call in calls[:2]:
                        options = dict(argument.split('=', 1)
                                       for index, argument in enumerate(call)
                                       if index and call[index - 1] == '-o')
                        self.assertEqual({name: options.get(name) for name in expected},
                                         expected)
                    self.assertIn('APT::Update::Error-Mode=any', calls[0])
                    self.assertEqual(calls[2][1], 'scripts/setup_dev_tools.py')
                    self.assertEqual(calls[3][1:3], ['-m', 'pip'])
                    self.assertEqual(published.read_text(), str(root / '.venv/bin') + '\n')
                    self.assertEqual(step['timeout-minutes'], '10')
                    self.assertNotIn('continue-on-error', step)

    def test_whole_index_tdd_hook_never_receives_changed_filenames(self):
        document = yaml.safe_load(
            (ROOT / '.pre-commit-config.yaml').read_text())
        hook = next(hook for repository in document['repos']
                    for hook in repository['hooks']
                    if hook['id'] == 'nexus-example-tdd')
        # The entry validates the complete index itself and accepts no paths.
        self.assertEqual(hook['entry'],
                         'python scripts/tdd_gate.py --staged')
        self.assertIs(hook['pass_filenames'], False)
        self.assertIs(hook['always_run'], True)
        self.assertIs(hook['require_serial'], True)

    def test_python_tdd_hook_declares_the_pinned_workflow_parser(self):
        pins = [line for line in
                (sdk_root() / 'requirements.txt').read_text().splitlines()
                if line.startswith('PyYAML==')]
        self.assertEqual(len(pins), 1)
        document = yaml.load((ROOT / '.pre-commit-config.yaml').read_text(),
                             Loader=yaml.BaseLoader)
        hook = next(hook for repository in document['repos']
                    for hook in repository['hooks']
                    if hook['id'] == 'nexus-example-tdd')
        self.assertEqual(hook['language'], 'python')
        self.assertIn(pins[0], hook.get('additional_dependencies', []))

    def test_complete_google_execution_is_retained(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        steps = document['jobs']['native']['steps']
        artifact = next(step for step in steps
                        if step.get('name') ==
                        'Retain Native artifacts and execution evidence')
        self.assertIn('build/native-debug/tests/google-execution/',
                      artifact['with']['path'])

    def test_uart_owner_firmware_has_an_independent_actual_elf_budget(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        steps = document['jobs']['arm']['steps']
        commands = '\n'.join(step.get('run', '') for step in steps)
        self.assertIn('/bin/uart_owner_runtime.elf', commands)
        self.assertIn('/uart-owner-resource-budget.json', commands)
        artifact = next(step for step in steps
                        if step.get('name') ==
                        'Retain firmware and static validation evidence')
        self.assertIn('/uart-owner-resource-budget.json', artifact['with']['path'])

    def test_native_builds_a_verified_installed_sdk_consumer_with_exact_revision(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        steps = document['jobs']['native']['steps']
        prepare = next(step for step in steps if step.get('name') ==
                       'Prepare a complete publishable Source SDK')
        self.assertIn('cmake/package/package_source_sdk.py', prepare['run'])
        self.assertIn('--source "$GITHUB_WORKSPACE/external/nexus"', prepare['run'])
        self.assertNotIn('--development-fixture', prepare['run'])
        configure = next(step for step in steps if step.get('name') ==
                         'Configure the installed Native consumer')
        self.assertEqual(configure['env']['CANDIDATE_NEXUS_REF'],
                         '${{ inputs.candidate_nexus_ref }}')
        self.assertIn('dependencies/nexus.lock.json', configure['run'])
        self.assertIn('SDK_COMMIT="$CANDIDATE_NEXUS_REF"', configure['run'])
        self.assertIn('-S reproduction -B build/installed-native', configure['run'])
        self.assertIn('-DNexus_DIR="$RUNNER_TEMP/nexus-source-sdk/lib/cmake/Nexus"',
                      configure['run'])
        self.assertIn('-DNEXUS_EXPECTED_SOURCE_REVISION="$SDK_COMMIT"', configure['run'])
        build = next(step for step in steps if step.get('name') ==
                     'Build the installed Native consumer')
        self.assertIn('cmake --build build/installed-native', build['run'])
        self.assertLess(steps.index(prepare), steps.index(configure))
        self.assertLess(steps.index(configure), steps.index(build))

    def test_installed_consumer_requires_fresh_nonempty_ctest_and_retained_evidence(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        steps = document['jobs']['native']['steps']
        execute = next(step for step in steps if step.get('name') ==
                       'Execute and validate the installed Native consumer')
        self.assertIn('rm -f "$report"', execute['run'])
        self.assertIn('time.time_ns()', execute['run'])
        self.assertIn('--no-tests=error', execute['run'])
        self.assertIn('--output-junit "$report"', execute['run'])
        self.assertIn('scripts/validation/junit.py', execute['run'])
        self.assertIn('--not-before-ns "$started_ns"', execute['run'])
        self.assertIn('set -euo pipefail', execute['run'])
        artifact = next(step for step in steps if step.get('name') ==
                        'Retain Native artifacts and execution evidence')
        for path in ('source-sdk-manifest.json', 'sdk-preparation.json',
                     'configure.log', 'build.log', 'ctest.log',
                     'junit-validation.log', 'ctest-results.xml',
                     'CMakeCache.txt', 'compile_commands.json',
                     'nexus-sdk/generated/*', 'bin/*'):
            self.assertIn('build/installed-native/' + path,
                          artifact['with']['path'])

    def test_advanced_h743_recipes_run_each_real_build_and_stop_on_failure(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        steps = document['jobs']['arm']['steps']
        advanced = next(step for step in steps if step.get('name') ==
                        'Build Cache-on receive and fixed-run pause recipes')
        self.assertEqual(advanced['if'],
                         "matrix.preset == 'stm32-h743-nucleo-baremetal'")
        stub = f'#!{sys.executable}\n' + r'''import json, os, sys
from pathlib import Path
command = Path(sys.argv[0]).name
arguments = sys.argv[1:]
with open(os.environ['ADVANCED_LOG'], 'a') as stream:
    stream.write(json.dumps([command, *arguments]) + '\n')
if command == 'cmake' and arguments[:1] == ['--build']:
    target = arguments[arguments.index('--target') + 1]
    if os.environ.get('ADVANCED_FAIL') == target:
        raise SystemExit(17)
'''
        expected = ['continuous_rx_cache_on', 'runtime_pause']
        for failure in ('', expected[0]):
            with self.subTest(failure=failure), \
                    tempfile.TemporaryDirectory(prefix='advanced recipe ') as value:
                root = Path(value)
                tools = root / 'tools'
                tools.mkdir()
                for name in ('cmake', 'python3'):
                    tool = tools / name
                    tool.write_text(stub)
                    tool.chmod(0o755)
                log = root / 'commands.jsonl'
                environment = dict(os.environ, GITHUB_WORKSPACE=str(root),
                                   PRESET="stm32-h743-nucleo-baremetal",
                                   ADVANCED_LOG=str(log), ADVANCED_FAIL=failure)
                environment['PATH'] = str(tools) + os.pathsep + environment['PATH']
                result = subprocess.run(
                    ['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail',
                     '-c', advanced['run']], cwd=root, env=environment,
                    text=True, capture_output=True, timeout=10)
                calls = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual(result.returncode, 17 if failure else 0,
                                 result.stdout + result.stderr)
                builds = [call for call in calls if call[:2] == ['cmake', '--build']]
                self.assertEqual([call[call.index('--target') + 1] for call in builds],
                                 expected[:1] if failure else expected)
                resources = [call for call in calls if call[0] == 'python3']
                self.assertEqual(len(resources), 0 if failure else 2)
                for call, target in zip(resources, expected):
                    self.assertIn('/advanced/' + target + '/bin/' + target + '.elf',
                                  call[call.index('--elf') + 1])
                    self.assertIn('/advanced/' + target + '/resource-budget.json',
                                  call[call.index('--report') + 1])
        artifact = next(step for step in steps if step.get('name') ==
                        'Retain firmware and static validation evidence')
        self.assertIn('build/${{ matrix.preset }}/advanced/',
                      artifact['with']['path'])

    def test_arm_hil_preparation_never_substitutes_an_unreviewed_reference_fixture(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        step = next(step for step in document['jobs']['arm']['steps']
                    if step.get('name') == 'Prepare the unbound physical station work plan')
        self.assertIn("!startsWith(matrix.preset, 'stm32-f103-reference-')", step['if'])
        self.assertIn("!startsWith(matrix.preset, 'gd32-f303-reference-')", step['if'])
        self.assertIn('stm32-g474-reference-*) fixture=stm32g474re_hse24_reference',
                      step['run'])


if __name__ == '__main__':
    unittest.main()
