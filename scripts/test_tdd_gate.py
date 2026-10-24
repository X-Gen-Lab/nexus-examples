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
        self.google_specs = {}
        for index, name in enumerate(
                name for name in gate.EXPECTED_CTEST_NAMES
                if name.endswith("_google")):
            output = ("google-execution" if name == "owner_lifecycle_google"
                      else name + "-execution")
            self.google_specs[name] = {
                "binary": self.root / "build/native-debug/tests" / (name + "_test"),
                "output": self.root / "build/native-debug/tests" / output,
                "cases": index + 1,
            }
        self.secondary = next(name for name in self.google_specs
                              if name != "owner_lifecycle_google")

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.root, check=True,
                              capture_output=True, text=True).stdout.strip()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def execute(self, root, arguments, environment, log):
        """Mock commands only; real SDK validators parse each produced XML."""
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
                names = list(gate.EXPECTED_CTEST_NAMES)
                names.extend(name for name in self.google_specs
                             if name not in names)
                if self.mode == "partial":
                    names = names[:-1]
                cases = "".join('<testcase name="' + name + '">' + skipped +
                                "</testcase>" for name in names)
                self.write("build/native-debug/ctest-results.xml",
                           "<testsuite>" + cases + "</testsuite>")
            for name, spec in self.google_specs.items():
                binary, output = spec["binary"], spec["output"]
                self.write(str(binary.relative_to(self.root)), "fixture binary\n")
                case_names = ["CompleteCase" + (str(i) if i else "")
                              for i in range(spec["cases"])]
                listing = "Fixture.\n" + "".join("  " + case + "\n"
                                                for case in case_names)
                self.write(str((output / "discovery.log").relative_to(self.root)),
                           listing)
                testcases = "".join(
                    '<testcase classname="Fixture" name="' + case +
                    '" status="run" result="completed"/>' for case in case_names)
                report = output / "google-results.xml"
                self.write(str(report.relative_to(self.root)),
                           '<testsuites name="AllTests"><testsuite name="Fixture">'
                           + testcases + '</testsuite></testsuites>')
                if name == self.secondary:
                    if self.mode == "secondary_missing":
                        report.unlink()
                    elif self.mode == "secondary_stale":
                        os.utime(output / "discovery.log", ns=(1, 1))
                    elif self.mode == "secondary_wrong_identity":
                        (output / "discovery.log").write_text(
                            listing.replace("CompleteCase", "OtherCase"))
                    elif self.mode == "secondary_skipped":
                        report.write_text(report.read_text().replace(
                            'status="run" result="completed"',
                            'status="notrun" result="suppressed"'))
                    elif self.mode == "secondary_symlink":
                        retained = self.root / "retained.xml"
                        retained.write_text(report.read_text())
                        report.unlink()
                        report.symlink_to(retained)
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
            names.extend(name for name in self.google_specs if name not in names)
            if self.mode == "filtered" and "--preset" not in arguments:
                names.append("additional_actual_test")
            entries = []
            for name in names:
                entry = {"name": name, "properties": []}
                if name in self.google_specs:
                    spec = self.google_specs[name]
                    entry["properties"] = [
                        {"name": "LABELS", "value": ["native", "GoogleTest"]}]
                    entry["command"] = [
                        "/python", "-B", str(self.root / "scripts/run_google.py"),
                        "--nexus-source", str(self.root / "external/nexus"),
                        "--binary", str(spec["binary"]),
                        "--output", str(spec["output"])]
                    if name == self.secondary:
                        if self.mode == "duplicate_output":
                            entry["command"][-1] = str(
                                self.google_specs["owner_lifecycle_google"]["output"])
                        elif self.mode == "duplicate_binary":
                            entry["command"][-3] = str(
                                self.google_specs["owner_lifecycle_google"]["binary"])
                        elif self.mode == "missing_label":
                            entry["properties"] = []
                        elif self.mode == "repeated_output_argument":
                            entry["command"].extend(["--output", str(spec["output"])])
                entries.append(entry)
            return json.dumps({"tests": entries})
        return ""

    def run_gate(self, paths=("apps/example.c",), force=False, environ=None):
        with mock.patch.object(gate, "execute", side_effect=self.execute), \
                mock.patch.object(gate, "load", return_value=self.sdk_gate):
            return gate.run_gate(self.root, paths=paths, staged=True, force=force,
                                 environ={"PATH": os.environ["PATH"],
                                          **(environ or {})})

    def test_relocated_sdk_reaches_both_identity_checks_and_the_build(self):
        with tempfile.TemporaryDirectory(prefix="relocated formal SDK ") as value:
            source = Path(value) / "sdk"
            shutil.copytree(self.root / "external/nexus", source)
            index = self.git("write-tree")
            self.run_gate(environ={"NEXUS_SOURCE": str(source)})
            selected_commands = [command for command, _ in self.commands
                                 if str(command[1]).endswith((
                                     "test_repository.py", "build.py"))]
            self.assertEqual(len(selected_commands), 3)
            for command in selected_commands:
                self.assertIn("--nexus-source", command)
                self.assertEqual(command[command.index("--nexus-source") + 1],
                                 str(source))
                self.assertNotIn("--candidate-nexus-commit", command)
            self.assertTrue(all(environment["NEXUS_SOURCE"] == str(source)
                                for _, environment in self.commands))
            self.assertEqual(self.git("write-tree"), index)
            self.commands.clear()
            self.mode = "wrong_lock"
            with self.assertRaisesRegex(ValueError, "formal SDK"):
                self.run_gate(environ={"NEXUS_SOURCE": str(source)})
            self.assertEqual(len(self.commands), 1)
            self.assertEqual(self.git("write-tree"), index)

    def test_parallel_jobs_precedence_reaches_the_actual_build_command(self):
        for environ, expected in (
                ({"NEXUS_BUILD_JOBS": "1", "CMAKE_BUILD_PARALLEL_LEVEL": "3"}, "1"),
                ({"CMAKE_BUILD_PARALLEL_LEVEL": "3"}, "3"),
                ({}, "2")):
            with self.subTest(environ=environ):
                index = self.git("write-tree")
                self.commands.clear()
                self.run_gate(environ=environ)
                builds = [command for command, _ in self.commands
                          if "build.py" in str(command[1])]
                self.assertEqual(len(builds), 1)
                command = builds[0]
                self.assertEqual(command[command.index("--parallel") + 1], expected)
                self.assertEqual(self.git("write-tree"), index)

    def test_invalid_parallel_jobs_reject_before_external_commands_or_index_change(self):
        for name in ("NEXUS_BUILD_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL"):
            for value in ("0", "-1", "many"):
                with self.subTest(name=name, value=value):
                    index = self.git("write-tree")
                    self.commands.clear()
                    with mock.patch.object(gate, "git", side_effect=AssertionError(
                            "invalid jobs invoked an external Git command")):
                        with self.assertRaisesRegex(ValueError,
                                                    "parallel jobs.*positive integer"):
                            self.run_gate(environ={name: value})
                    self.assertFalse(self.commands)
                    self.assertEqual(self.git("write-tree"), index)

    def test_workflow_changes_select_the_gate_and_reject_snapshot_drift(self):
        workflow = ".github/workflows/ci.yml"
        self.write(workflow, "name: staged workflow\n")
        self.git("add", workflow)
        index = self.git("write-tree")
        self.assertIsNotNone(self.run_gate(paths=(workflow,)))
        self.assertEqual(len(self.commands), 5)
        self.assertEqual(self.git("write-tree"), index)
        for name in (workflow, ".github/workflows/untracked.yml"):
            with self.subTest(path=name):
                self.commands.clear()
                self.write(name, "name: changed workflow\n")
                with self.assertRaisesRegex(ValueError, "Unstaged"):
                    self.run_gate(paths=(workflow,))
                self.assertFalse(self.commands)
                self.assertEqual(self.git("write-tree"), index)
                if name == workflow:
                    self.git("restore", workflow)
                else:
                    (self.root / name).unlink()

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

    def test_all_labelled_google_programs_are_reported_without_extra_execution(self):
        report = self.run_gate()
        programs = report.get("google_programs", [])
        self.assertEqual(len(programs), len(self.google_specs))
        self.assertEqual({program["ctest_name"] for program in programs},
                         set(self.google_specs))
        aggregate = report.get("google_aggregate", {})
        self.assertEqual(aggregate.get("programs"), len(self.google_specs))
        self.assertEqual(aggregate.get("passed"),
                         sum(spec["cases"] for spec in self.google_specs.values()))
        self.assertEqual(report["google"]["passed"], 1)
        self.assertEqual(len(self.commands), 5)

    def test_an_added_actual_google_label_is_aggregated_without_a_name_list(self):
        self.google_specs["new_actual_contract"] = {
            "binary": self.root / "build/native-debug/tests/new-contract",
            "output": self.root / "build/native-debug/tests/new-execution",
            "cases": 2,
        }
        report = self.run_gate()
        self.assertIn("new_actual_contract",
                      {p["ctest_name"] for p in report.get("google_programs", [])})
        self.assertEqual(report.get("google_aggregate", {}).get("programs"),
                         len(self.google_specs))

    def test_secondary_google_missing_stale_skipped_or_wrong_identity_rejects(self):
        for mode in ("secondary_missing", "secondary_stale",
                     "secondary_skipped", "secondary_wrong_identity"):
            with self.subTest(mode=mode):
                self.mode = mode
                with self.assertRaises((ValueError, OSError)):
                    self.run_gate()
                self.assertFalse((self.root / "build/native-debug/tdd/execution.json")
                                 .exists())

    def test_secondary_google_symlink_is_not_followed_as_execution_evidence(self):
        self.mode = "secondary_symlink"
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.run_gate()
        self.assertIn("AllTests", (self.root / "retained.xml").read_text())

    def test_ambiguous_catalogue_google_identity_or_output_is_rejected(self):
        for mode in ("duplicate_output", "duplicate_binary", "missing_label",
                     "repeated_output_argument"):
            with self.subTest(mode=mode):
                self.mode = mode
                with self.assertRaises(ValueError):
                    self.run_gate()

    def test_cli_prints_the_aggregate_instead_of_only_the_legacy_program(self):
        report = {"google": {"passed": 1},
                  "google_aggregate": {"passed": 15, "programs": 5}}
        with mock.patch.object(gate, "run_gate", return_value=report), \
                mock.patch("builtins.print") as output:
            self.assertEqual(gate.main([]), 0)
        self.assertIn("15 GoogleTest cases", output.call_args.args[0])
        self.assertIn("5 programs", output.call_args.args[0])

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
        self.assertEqual(environment["NEXUS_SOURCE"], "/wrong/sdk")
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
