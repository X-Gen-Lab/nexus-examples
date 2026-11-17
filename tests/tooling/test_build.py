"""Execute the thin runner against real finite/empty/skipped CTest projects.

These temporary CMake fixtures validate orchestration and report rejection;
they are not SDK or hardware qualification.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from sdk_tools import maintenance

HERE = Path(__file__).resolve().parents[2] / "scripts"
spec = importlib.util.spec_from_file_location("examples_build", HERE / "build.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class BuildReportContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="examples runner ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / "sdk"
        self.sdk.mkdir()
        (self.sdk / "CMakeLists.txt").write_text("# developer-tooling fixture\n")
        maintained = Path(os.environ.get("NEXUS_SOURCE", str(HERE.parent / "external/nexus"))) / "scripts/validation/junit.py"
        self.assertTrue(maintained.is_file(), "initialize the pinned Nexus SDK")
        helper = self.sdk / "scripts/validation/junit.py"
        helper.parent.mkdir(parents=True)
        shutil.copyfile(maintained, helper)
        presets = self.sdk / "tools/dev/presets.py"
        presets.parent.mkdir(parents=True)
        shutil.copyfile(maintained.parents[2] / "tools/dev/presets.py", presets)
        # The temporary project models CMake/JUnit only. Its external execution
        # dependency is the real pinned SDK, never a partial tools namespace.
        patcher = mock.patch.object(runner, "maintenance",
                                    return_value=maintenance(maintained.parents[2]))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.previous = runner.ROOT
        runner.ROOT = self.root
        self.addCleanup(setattr, runner, "ROOT", self.previous)
        self.assertIsNotNone(shutil.which("cmake"), "CMake is required")
        (self.root / "CMakePresets.json").write_text(json.dumps({
            "version": 3,
            "configurePresets": [{"name": "native-debug", "generator": "Ninja",
                "binaryDir": "${sourceDir}/build/native-debug"}],
            "buildPresets": [{"name": "native-debug", "configurePreset": "native-debug"}],
            "testPresets": [{"name": "native-debug", "configurePreset": "native-debug"}]}))

    def execute(self, mode, preset="native-debug", output=None):
        (self.root / "main.c").write_text("int main(void) { return 0; }\n")
        content = "cmake_minimum_required(VERSION 3.21)\nproject(finite_fixture C)\nenable_testing()\n"
        if mode == "finite":
            content += "add_executable(finite main.c)\nadd_test(NAME finite COMMAND finite)\n"
        elif mode == "skip":
            (self.root / "main.c").write_text("int main(void) { return 77; }\n")
            content += "add_executable(finite main.c)\nadd_test(NAME finite COMMAND finite)\nset_tests_properties(finite PROPERTIES SKIP_RETURN_CODE 77)\n"
        (self.root / "CMakeLists.txt").write_text(content)
        stale = (output or self.root / "build/native-debug") / "ctest-results.xml"
        stale.parent.mkdir(parents=True)
        stale.write_text('<testsuite tests="1"><testcase name="old-pass"/></testsuite>')
        result = runner.main(["--preset", preset, "--nexus-source", str(self.sdk),
                              "--test", "--parallel", "2"])
        return result, stale

    def test_finite_execution_replaces_stale_report(self):
        result, report = self.execute("finite")
        self.assertEqual(result, 0)
        self.assertNotIn("old-pass", report.read_text())

    def test_empty_execution_fails(self):
        result, _ = self.execute("zero")
        self.assertNotEqual(result, 0)

    def test_all_skipped_execution_fails(self):
        result, _ = self.execute("skip")
        self.assertNotEqual(result, 0)

    def test_user_preset_owns_the_actual_log_and_report_directory(self):
        output = self.root / "custom output" / "firmware"
        (self.root / "CMakeUserPresets.json").write_text(json.dumps({
            "version": 3,
            "configurePresets": [{"name": "local", "inherits": "native-debug",
                                  "binaryDir": str(output)}],
            "buildPresets": [{"name": "local-build", "configurePreset": "local"}],
            "testPresets": [{"name": "local-build", "configurePreset": "local"}]}))
        result, report = self.execute("finite", "local-build", output)
        self.assertEqual(result, 0)
        self.assertTrue((output / "build.log").is_file())
        self.assertNotIn("old-pass", report.read_text())
        self.assertFalse((self.root / "build/local-build").exists())

    def test_missing_test_preset_rejects_before_configure(self):
        path = self.root / "CMakePresets.json"
        document = json.loads(path.read_text())
        document.pop("testPresets")
        path.write_text(json.dumps(document))
        with mock.patch.object(runner, "run", return_value=0) as command:
            result = runner.main(["--preset", "native-debug", "--nexus-source",
                                  str(self.sdk), "--test"])
        self.assertNotEqual(result, 0)
        command.assert_not_called()
        self.assertFalse((self.root / "build").exists())

    def test_test_preset_for_another_configuration_rejects_before_build(self):
        path = self.root / "CMakePresets.json"
        document = json.loads(path.read_text())
        document["configurePresets"].append({
            "name": "other", "inherits": "native-debug",
            "binaryDir": "${sourceDir}/build/other"})
        document["testPresets"][0]["configurePreset"] = "other"
        path.write_text(json.dumps(document))
        with mock.patch.object(runner, "run", return_value=0) as command:
            result = runner.main(["--preset", "native-debug", "--nexus-source",
                                  str(self.sdk), "--test"])
        self.assertNotEqual(result, 0)
        command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
