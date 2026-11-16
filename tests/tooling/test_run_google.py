"""Execute report/discovery failures with a synthetic framework-boundary binary."""

import json
import os
from pathlib import Path
import tempfile
import unittest

from run_google import run_google
from sdk_tools import sdk_root


class GoogleExecutionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / "framework-fixture"
        self.binary.write_text(
            "#!/usr/bin/env python3\n"
            "import os,sys\n"
            "from pathlib import Path\n"
            "if '--gtest_list_tests' in sys.argv:\n"
            " print('Fixture.\\n  RealCase')\n"
            " sys.exit(0)\n"
            "target=next(x.split('xml:',1)[1] for x in sys.argv "
            "if x.startswith('--gtest_output=xml:'))\n"
            "mode=os.environ.get('FIXTURE_MODE','pass')\n"
            "if mode=='missing': sys.exit(0)\n"
            "if mode=='fail': sys.exit(1)\n"
            "status='notrun' if mode=='skip' else 'run'\n"
            "result='suppressed' if mode=='skip' else 'completed'\n"
            "name='OtherCase' if mode=='changed' else 'RealCase'\n"
            "Path(target).write_text('<testsuites name=\"AllTests\" tests=\"1\" "
            "failures=\"0\" errors=\"0\" skipped=\"0\"><testsuite "
            "name=\"Fixture\" tests=\"1\"><testcase classname=\"Fixture\" "
            "name=\"'+name+'\" status=\"'+status+'\" result=\"'+result+'\"/>"
            "</testsuite></testsuites>')\n")
        self.binary.chmod(0o755)

    def run_fixture(self, mode):
        environment = {**os.environ, "FIXTURE_MODE": mode}
        return run_google(sdk_root(), self.binary, self.root / mode,
                          environment=environment)

    def test_actual_complete_discovery_bound_execution_passes(self):
        report = self.run_fixture("pass")
        self.assertEqual(report["passed"], 1)
        self.assertEqual(report["discovered"], 1)
        self.assertFalse(report["qualifies_source_sdk"])
        self.assertEqual(len(report["commands"]), 2)

    def test_missing_skipped_changed_and_failed_execution_rejects(self):
        for mode in ("missing", "skip", "changed", "fail"):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.run_fixture(mode)

    def test_output_directory_cannot_replace_binary_or_unowned_content(self):
        with self.assertRaises(ValueError):
            run_google(sdk_root(), self.binary, self.root)
        other = self.root / "unowned"
        other.mkdir()
        retained = other / "retained.json"
        retained.write_text(json.dumps({"raw": "keep"}))
        with self.assertRaises(ValueError):
            run_google(sdk_root(), self.binary, other)
        self.assertEqual(json.loads(retained.read_text()), {"raw": "keep"})


    def test_owned_output_rejects_symlinked_log_before_any_input_write(self):
        self.run_fixture("pass")
        output = self.root / "pass"
        log = output / "execution.log"
        log.unlink()
        log.symlink_to(self.binary)
        before = self.binary.read_bytes()
        with self.assertRaises(ValueError):
            run_google(sdk_root(), self.binary, output)
        self.assertEqual(self.binary.read_bytes(), before)

    def test_failed_repeat_invalidates_previous_positive_execution_summary(self):
        output = self.root / "pass"
        self.run_fixture("pass")
        self.assertTrue((output / "execution.json").is_file())
        with self.assertRaises(ValueError):
            run_google(sdk_root(), self.binary, output,
                       environment={**os.environ, "FIXTURE_MODE": "missing"})
        self.assertFalse((output / "execution.json").exists())


if __name__ == "__main__":
    unittest.main()
