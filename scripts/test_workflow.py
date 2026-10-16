"""Keep real example execution and every firmware budget in CI evidence."""

from pathlib import Path
import unittest

import yaml

from sdk_tools import sdk_root


ROOT = Path(__file__).resolve().parents[1]


class ExampleWorkflow(unittest.TestCase):
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
