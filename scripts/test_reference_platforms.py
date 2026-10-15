"""Require explicit, isolated H743/U575 software-reference compositions."""

import json
from pathlib import Path
import tempfile
import tomllib
import unittest
from unittest import mock

import yaml

from sdk_tools import ROOT
import test_repository as repository
import tdd_gate


REFERENCE_BOARDS = {
    "h743": ("stm32h743_nucleo_zi2", "hsi64-fixed-cache-off", "uart3"),
    "u575": ("stm32u575_nucleo_zi_q", "msis4", "uart1"),
}


class ReferencePlatformTests(unittest.TestCase):
    def test_commit_gate_requires_every_actual_google_program(self):
        for name in ("owner_lifecycle_google", "product_fault_policy_google",
                     "reference_rx_policy_google", "reference_lifecycle_google"):
            with self.subTest(program=name):
                self.assertIn(name, tdd_gate.EXPECTED_CTEST_NAMES)

    def test_source_identity_requires_both_reference_device_closures(self):
        self.assertIn("vendors/st/cmsis_device_h7", repository.DEPENDENCIES)
        self.assertIn("vendors/st/cmsis_device_u5", repository.DEPENDENCIES)

    def test_source_identity_requires_the_selected_official_f4_driver(self):
        self.assertIn("vendors/st/stm32f4_hal", repository.DEPENDENCIES)

    def test_reference_identity_rejects_a_same_count_unmaintained_preset(self):
        document = json.loads((ROOT / "CMakePresets.json").read_text())
        original = document["configurePresets"][-1]["name"]
        for section in ("configurePresets", "buildPresets"):
            for entry in document[section]:
                if entry["name"] == original:
                    entry["name"] = "unmaintained-board"
                    if "configurePreset" in entry:
                        entry["configurePreset"] = "unmaintained-board"
        with tempfile.TemporaryDirectory() as value:
            root = Path(value)
            (root / "CMakePresets.json").write_text(json.dumps(document))
            with mock.patch.object(repository, "ROOT", root):
                with self.assertRaisesRegex(ValueError, "reference matrix"):
                    repository.verify_presets()

    def test_each_reference_board_has_two_explicit_public_compositions(self):
        document = json.loads((ROOT / "CMakePresets.json").read_text())
        presets = {entry["name"]: entry for entry in document["configurePresets"]}
        builds = {entry["name"]: entry for entry in document["buildPresets"]}
        for family, (board, clock, binding) in REFERENCE_BOARDS.items():
            for backend in ("baremetal", "freertos"):
                name = f"stm32-{family}-nucleo-{backend}"
                with self.subTest(preset=name):
                    self.assertIn(name, presets)
                    self.assertEqual(presets[name]["binaryDir"],
                                     "${sourceDir}/build/" + name)
                    self.assertEqual(builds[name]["configurePreset"], name)
                    assembly = presets[name]["cacheVariables"]["NEXUS_ASSEMBLY_FILE"]
                    data = tomllib.loads(Path(assembly.replace(
                        "${sourceDir}", str(ROOT))).read_text())
                    self.assertEqual(data["board"], board)
                    self.assertEqual(data["clock"], clock)
                    self.assertEqual(data["backend"], backend)
                    self.assertEqual(set(data["gpio"]), {"led0"})
                    self.assertEqual(data["gpio"]["led0"]["binding"], "led0")
                    self.assertEqual(set(data["uart"]), {"uart1"})
                    uart = data["uart"]["uart1"]
                    self.assertEqual(uart["binding"], binding)
                    self.assertEqual(uart["mode"], "irq-bytes")
                    self.assertEqual(uart["baud"], 115200)
                    self.assertEqual(uart["rx"], {"format": "bytes", "capacity": 32})
                    self.assertEqual(uart["irq"]["priority"], 5)
                    self.assertEqual(uart["irq"]["calls_os"], backend == "freertos")
                    self.assertEqual(data["components"], ["uart-owner"])
                    self.assertEqual(data["memory"]["libc_heap_bytes"], 0)
                    self.assertNotIn("dma", uart)

    def test_ci_builds_every_arm_preset_and_retains_reference_google_evidence(self):
        workflow = yaml.load((ROOT / ".github/workflows/ci.yml").read_text(),
                             Loader=yaml.BaseLoader)
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        expected = {entry["name"] for entry in presets["configurePresets"]
                    if entry["name"] != "native-debug"}
        matrix = workflow["jobs"]["arm"]["strategy"]["matrix"]["preset"]
        for family in REFERENCE_BOARDS:
            for backend in ("baremetal", "freertos"):
                self.assertIn(f"stm32-{family}-nucleo-{backend}", matrix)
        self.assertEqual(set(matrix), expected)
        paths = next(step["with"]["path"]
                     for step in workflow["jobs"]["native"]["steps"]
                     if step.get("name") == "Retain Native artifacts and execution evidence")
        self.assertIn("build/native-debug/tests/reference-google-execution/", paths)
        self.assertIn("build/native-debug/tests/lifecycle-google-execution/", paths)


if __name__ == "__main__":
    unittest.main()
