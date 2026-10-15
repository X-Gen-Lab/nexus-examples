"""Keep real example execution and every firmware budget in CI evidence."""

from pathlib import Path
import unittest

import yaml


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
                (ROOT / 'external/nexus/requirements.txt').read_text().splitlines()
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


if __name__ == '__main__':
    unittest.main()
