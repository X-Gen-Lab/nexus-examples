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


ROOT = Path(__file__).resolve().parents[2]


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

    def test_workflow_delegates_real_software_commands_to_local_ci(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        for job, name, scope in (
                ('native', 'Run complete local Native and installed SDK integration', 'native'),
                ('arm', 'Run the maintained ARM integration', 'arm')):
            step = next(row for row in document['jobs'][job]['steps']
                        if row.get('name') == name)
            with tempfile.TemporaryDirectory(prefix='workflow delegation ') as value:
                root = Path(value)
                tools = root / 'bin'
                tools.mkdir()
                target = tools / 'python3'
                target.write_text(f'#!{sys.executable}\n' +
                                  'import json,os,sys\n' +
                                  'open(os.environ["CALL"],"w").write(json.dumps(sys.argv[1:]))\n')
                target.chmod(0o755)
                environment = dict(os.environ, PATH=str(tools)+os.pathsep+os.environ['PATH'],
                                   CALL=str(root / 'call.json'), PRESET='stm32-sky-baremetal',
                                   RUNNER_TEMP=str(root))
                result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', step['run']],
                                        env=environment, text=True, capture_output=True,
                                        timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                arguments = json.loads((root / 'call.json').read_text())
                self.assertEqual(arguments[0], 'scripts/local_ci.py')
                self.assertEqual(arguments[arguments.index('--scope')+1], scope)
                self.assertIn('--output', arguments)
                if scope == 'arm':
                    self.assertEqual(arguments[arguments.index('--preset')+1],
                                     'stm32-sky-baremetal')
                    self.assertIn('--arm-toolchain', arguments)

    def test_arm_matrix_is_projected_from_maintained_presets(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        self.assertEqual(document['jobs']['arm']['strategy']['matrix']['preset'],
                         '${{ fromJSON(needs.native.outputs.arm_presets) }}')
        result = subprocess.run([sys.executable, 'scripts/local_ci.py', '--list-arm-presets'],
                                cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        presets = json.loads((ROOT / 'CMakePresets.json').read_text())
        expected = {row['name'] for row in presets['configurePresets']
                    if not row.get('hidden') and row['name'] != 'native-debug'}
        self.assertEqual(set(json.loads(result.stdout)), expected)
        self.assertEqual(len(expected), 8)

    def test_current_results_and_managed_failure_logs_are_retained(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        for job, name, paths in (
                ('native', 'Retain Native artifacts and execution evidence',
                 ('build/ci-native/', 'build/native-debug/tdd/',
                  'build/native-debug/tests/*execution/',
                  'build/native-debug/ctest-results.xml')),
                ('arm', 'Retain firmware and static validation evidence',
                 ('build/ci-${{ matrix.preset }}/',
                  'build/${{ matrix.preset }}/bin/',
                  'build/${{ matrix.preset }}/nexus/generated/'))):
            artifact = next(row for row in document['jobs'][job]['steps']
                            if row.get('name') == name)
            self.assertEqual(artifact['if'], 'always()')
            for path in paths:
                self.assertIn(path, artifact['with']['path'])

    def test_checkout_prepares_selected_dependencies_before_shared_execution(self):
        document = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(),
                             Loader=yaml.BaseLoader)
        for job in document['jobs'].values():
            steps = job['steps']
            checkouts = [row for row in steps
                         if row.get('uses', '').startswith('actions/checkout@')]
            self.assertTrue(checkouts)
            self.assertTrue(all(row['with'].get('submodules') != 'recursive'
                                for row in checkouts))
            prepare = next(row for row in steps if row.get('name') ==
                           'Prepare selected SDK dependencies')
            self.assertIn('prepare --scope native --allow-network', prepare['run'])
            execute = next(row for row in steps if 'scripts/local_ci.py --scope' in row.get('run', ''))
            self.assertLess(steps.index(checkouts[-1]), steps.index(prepare))
            self.assertLess(steps.index(prepare), steps.index(execute))


if __name__ == '__main__':
    unittest.main()
