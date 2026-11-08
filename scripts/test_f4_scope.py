"""The current examples follow the maintained F407 and explicit host scope."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest

import local_ci
from sdk_tools import ROOT
import test_repository as repository


class F4ScopeTests(unittest.TestCase):
    def test_candidate_sdk_checks_the_current_official_f4_source(self):
        revision = subprocess.check_output(
            ["git", "-C", str(ROOT / "external/nexus"), "rev-parse", "HEAD"],
            text=True).strip()
        with tempfile.TemporaryDirectory() as value:
            report = Path(value) / "identity.json"
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/test_repository.py"),
                 "--candidate-nexus-commit", revision, "--report", str(report)],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            identity = json.loads(report.read_text())
            self.assertEqual(revision, identity["nexus_revision"])
            vendor = identity["stm32f4_vendor_verification"]
            self.assertEqual("nexus-stm32f4-official-source-verification",
                             vendor["kind"])
            self.assertFalse(vendor["software_qualified"])

    def test_complete_reference_matrix_uses_only_four_f407_boards(self):
        expected = {"native-debug"}
        expected.update(f"stm32-{board}-{backend}"
                        for board in ("qiming", "sky", "qiming-core", "qiming-v41")
                        for backend in ("baremetal", "freertos"))
        self.assertEqual(expected, set(repository.verify_presets()))

    def test_native_models_require_explicit_test_support(self):
        document = json.loads((ROOT / "CMakePresets.json").read_text())
        native = next(row for row in document["configurePresets"]
                      if row["name"] == "native-debug")
        self.assertEqual("ON", native["cacheVariables"].get("NEXUS_TEST_SUPPORT"))

    def test_retired_hardware_cannot_produce_current_hil_plans(self):
        for board in ("gd32-liangshan", "stm32-h743-nucleo", "stm32-u575-nucleo",
                      "stm32-f103-reference", "gd32-f303-reference",
                      "stm32-g474-reference"):
            with self.subTest(board=board):
                self.assertEqual([], local_ci.standalones(board + "-baremetal"))
                with self.assertRaises(ValueError):
                    local_ci.fixture_for(board + "-baremetal")

    def test_current_starters_are_native_or_f407(self):
        manifest = json.loads((ROOT / "templates/manifest.json").read_text())
        self.assertEqual({"native", "qiming", "sky", "qiming-core", "qiming-v41"},
                         set(manifest["templates"]))
        for name in manifest["templates"]:
            data = tomllib.loads((ROOT / "templates" / name /
                                  "assembly.toml").read_text())
            if name == "native":
                self.assertEqual("native", data["backend"])
            else:
                self.assertTrue(data["board"].startswith("stm32f407"), name)


if __name__ == "__main__":
    unittest.main()
