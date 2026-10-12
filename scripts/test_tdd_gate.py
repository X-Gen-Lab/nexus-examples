"""Exercise staged-source and execution boundaries with real Git fixtures."""

import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
from unittest import mock

import tdd_gate as gate
from sdk_tools import load, sdk_root


class ExampleTddGateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "-q")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "user.name", "Gate fixture")
        self.write(".gitignore", "/build/\n")
        self.write("apps/example.c", "int example;\n")
        self.write("README.md", "Fixture documentation.\n")
        self.git("add", ".")
        self.git("commit", "-qm", "test: initialize fixture")
        sdk = self.root / "external/nexus"
        sdk.mkdir(parents=True)
        for arguments in (("init", "-q"),
                          ("config", "user.email", "fixture@example.invalid"),
                          ("config", "user.name", "SDK fixture")):
            subprocess.run(["git", *arguments], cwd=sdk, check=True,
                           capture_output=True)
        (sdk / "CMakeLists.txt").write_text("# Synthetic SDK identity fixture\n")
        subprocess.run(["git", "add", "."], cwd=sdk, check=True, capture_output=True)
        subprocess.run(["git", "commit", "-qm", "test: SDK identity"], cwd=sdk,
                       check=True, capture_output=True)
        self.sdk_revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=sdk,
                                           check=True, capture_output=True,
                                           text=True).stdout.strip()
        self.git("add", "external/nexus")
        self.git("commit", "-qm", "test: pin fixture SDK")
        self.source = sdk_root()
        self.sdk_gate = load(self.source, "scripts/ci/tdd_gate.py",
                             "example_gate_fixture_validator")
        self.commands = []
        self.mode = "pass"
        self.mutate = None

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.root, check=True,
                              capture_output=True, text=True).stdout.strip()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def execute(self, root, arguments, environment, log):
        """Mock only external commands; real validators parse produced files."""
        self.commands.append((arguments, environment))
        log.write_text("Synthetic subprocess-boundary fixture.\n")
        if "test_repository.py" in str(arguments[1]):
            if self.mode == "sdk_dirty":
                raise ValueError("Nexus source has uncommitted changes")
            report = Path(arguments[arguments.index("--report") + 1])
            report.write_text(json.dumps({
                "kind": "example-source-dependency-identity",
                "candidate_validation": self.mode == "candidate",
                "source_revision_matches_lock": self.mode != "wrong_lock",
                "example_gitlink_verified": True,
                "nexus_revision": self.sdk_revision, "nexus_tree": "b" * 40,
            }))
        elif "build.py" in str(arguments[1]):
            if self.mode != "missing":
                skipped = '<skipped/>' if self.mode == "skip" else ""
                names = gate.EXPECTED_CTEST_NAMES
                if self.mode == "partial":
                    names = names[:-1]
                cases = "".join('<testcase name="' + name + '">' + skipped +
                                "</testcase>" for name in names)
                self.write("build/native-debug/ctest-results.xml",
                           "<testsuite>" + cases + "</testsuite>")
            self.write("build/native-debug/tests/google-execution/discovery.log",
                       "Fixture.\n  CompleteCase\n")
            self.write("build/native-debug/tests/google-execution/google-results.xml",
                       '<testsuites name="AllTests"><testsuite name="Fixture">'
                       '<testcase classname="Fixture" name="CompleteCase" '
                       'status="run" result="completed"/>'
                       '</testsuite></testsuites>')
            if self.mode == "stale":
                os.utime(self.root / "build/native-debug/ctest-results.xml",
                         ns=(1, 1))
            if self.mode == "wrong_google":
                self.write("build/native-debug/tests/google-execution/discovery.log",
                           "Fixture.\n  OtherCase\n")
            if self.mutate:
                self.mutate()
        elif arguments[0] == "ctest":
            names = list(gate.EXPECTED_CTEST_NAMES)
            if self.mode == "filtered" and "--preset" not in arguments:
                names.append("additional_actual_test")
            return json.dumps({"tests": [{"name": name} for name in names]})
        return ""

    def run_gate(self, paths=("apps/example.c",), force=False):
        with mock.patch.object(gate, "execute", side_effect=self.execute), \
                mock.patch.object(gate, "load", return_value=self.sdk_gate):
            return gate.run_gate(self.root, paths=paths, staged=True, force=force,
                                 environ={"PATH": os.environ["PATH"]})

    def test_staged_complete_execution_binds_index_and_source_identity(self):
        self.write("apps/example.c", "int changed;\n")
        self.git("add", "apps/example.c")
        index = self.git("write-tree")
        report = self.run_gate()
        self.assertEqual(report["index_tree"], index)
        self.assertEqual(report["ctest"]["passed"], len(gate.EXPECTED_CTEST_NAMES))
        self.assertEqual(report["google"]["passed"], 1)
        self.assertEqual(self.git("write-tree"), index)
        self.assertEqual(len(self.commands), 5)
        build = self.commands[2][0]
        self.assertIn("--test", build)
        self.assertNotIn("--candidate-nexus-commit", self.commands[0][0])

    def test_unstaged_and_untracked_behavior_rejected_before_execution(self):
        for name in ("apps/example.c", "tests/new_case.cpp"):
            with self.subTest(name=name):
                self.write(name, "int changed;\n")
                with self.assertRaisesRegex(ValueError, "Unstaged"):
                    self.run_gate()
                self.assertFalse(self.commands)
                if name == "apps/example.c":
                    self.git("restore", name)
                else:
                    (self.root / name).unlink()

    def test_documentation_only_commit_skips_without_build_or_git_mutation(self):
        self.write("README.md", "Updated docs.\n")
        self.git("add", "README.md")
        before = self.git("write-tree")
        self.assertIsNone(self.run_gate(paths=("README.md",)))
        self.assertFalse(self.commands)
        self.assertEqual(self.git("write-tree"), before)

    def test_force_runs_documentation_only_snapshot(self):
        self.assertIsNotNone(self.run_gate(paths=("README.md",), force=True))

    def test_index_change_and_worktree_fix_during_execution_rejects(self):
        for stage in (False, True):
            with self.subTest(stage=stage):
                def mutate():
                    self.write("apps/example.c", "int changed;\n")
                    if stage:
                        self.git("add", "apps/example.c")
                self.mutate = mutate
                with self.assertRaises(ValueError):
                    self.run_gate()
                self.git("restore", "--staged", "apps/example.c")
                self.git("restore", "apps/example.c")

    def test_dirty_sdk_and_candidate_or_mismatched_pin_are_not_qualified(self):
        for mode in ("sdk_dirty", "candidate", "wrong_lock"):
            with self.subTest(mode=mode):
                self.mode = mode
                with self.assertRaises(ValueError):
                    self.run_gate()
                self.assertFalse(any("build.py" in str(command[1])
                                     for command, _ in self.commands))
                self.commands.clear()

    def test_sdk_modified_during_build_cannot_qualify_initial_identity(self):
        def mutate_sdk():
            self.mode = "sdk_dirty"
        self.mutate = mutate_sdk
        with self.assertRaisesRegex(ValueError, "uncommitted"):
            self.run_gate()

    def test_missing_stale_skipped_and_partial_ctest_reports_reject(self):
        for mode in ("missing", "stale", "skip", "partial"):
            with self.subTest(mode=mode):
                self.mode = mode
                with self.assertRaises(ValueError):
                    self.run_gate()

    def test_google_case_identity_must_match_unfiltered_discovery(self):
        self.mode = "wrong_google"
        with self.assertRaisesRegex(ValueError, "identit"):
            self.run_gate()

    def test_preset_filter_cannot_hide_an_added_actual_ctest_case(self):
        self.mode = "filtered"
        with self.assertRaisesRegex(ValueError, "every discovered"):
            self.run_gate()

    def test_child_environment_uses_own_checkout_not_hook_git_index(self):
        supplied = {"PATH": os.environ["PATH"], "GIT_DIR": "/wrong/.git",
                    "GIT_INDEX_FILE": "/wrong/index", "GTEST_FILTER": "Other*",
                    "NEXUS_SOURCE": "/wrong/sdk", "OTHER": "retained"}
        environment = gate.child_environment(self.root, supplied)
        self.assertNotIn("GIT_DIR", environment)
        self.assertNotIn("GIT_INDEX_FILE", environment)
        self.assertNotIn("GTEST_FILTER", environment)
        self.assertEqual(environment["NEXUS_SOURCE"],
                         str(self.root / "external/nexus"))
        self.assertEqual(environment["OTHER"], "retained")

    def test_hook_bypass_is_rejected(self):
        for name in ("SKIP", "NEXUS_SKIP_TDD"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                gate.child_environment(self.root, {name: "yes"})

    def test_output_symlink_does_not_overwrite_retained_input(self):
        retained = self.root / "retained"
        retained.write_text("retain raw evidence\n")
        output = self.root / "build/native-debug/tdd"
        output.mkdir(parents=True)
        (output / "source.json").symlink_to(retained)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.run_gate()
        self.assertEqual(retained.read_text(), "retain raw evidence\n")

    def test_google_parent_symlink_cannot_delete_retained_report(self):
        retained = self.root / "retained"
        retained.mkdir()
        report = retained / "google-results.xml"
        report.write_text("retain raw evidence\n")
        parent = self.root / "build/native-debug/tests"
        parent.mkdir(parents=True)
        (parent / "google-execution").symlink_to(retained, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.run_gate()
        self.assertEqual(report.read_text(), "retain raw evidence\n")

    def test_actual_build_log_symlink_cannot_overwrite_retained_input(self):
        retained = self.root / "retained.txt"
        retained.write_text("preserved original bytes\n")
        build = self.root / "build/native-debug"
        build.mkdir(parents=True)
        (build / "build.log").symlink_to(retained)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.run_gate()
        self.assertEqual(retained.read_text(), "preserved original bytes\n")

    def test_alternate_hook_index_must_bind_its_own_gitlink_even_if_diff_ignores_it(self):
        alternate = self.root / ".git/alternative-index"
        shutil.copyfile(self.root / ".git/index", alternate)
        self.git("config", "diff.ignoreSubmodules", "all")
        with mock.patch.dict(os.environ, {"GIT_INDEX_FILE": str(alternate)}):
            self.git("update-index", "--cacheinfo",
                     "160000," + "c" * 40 + ",external/nexus")
            with self.assertRaisesRegex(ValueError, "Gitlink"):
                self.run_gate()
        self.assertIn(self.sdk_revision,
                      self.git("ls-files", "--stage", "external/nexus"))


if __name__ == "__main__":
    unittest.main()
