"""Require explicit, isolated application compositions per reference Board."""

import json
import copy
from pathlib import Path
import tempfile
import tomllib
import unittest
from unittest import mock

import yaml

from sdk_tools import ROOT
import verify_sdk as repository
import tdd_gate


REFERENCE_BOARDS = {
    "qiming": ("stm32f407_qiming_v31", "hse8-pll168", "uart1", "events"),
    "sky": ("stm32f407ve_sky_qingchun", "hse8-pll168", "uart1", "events"),
    "qiming-core": ("stm32f407zg_qiming_core", "hse8-pll168", "uart1", "events"),
    "qiming-v41": ("stm32f407zg_qiming_v41", "hse8-pll168", "uart1", "events"),
    "f103rb": ("stm32f103rb_nucleo", "hsi8", "uart2", "bytes"),
}


class ReferencePlatformTests(unittest.TestCase):
    def test_commit_gate_requires_every_actual_google_program(self):
        for name in ("owner_lifecycle_google", "product_fault_policy_google",
                     "reference_rx_policy_google", "reference_lifecycle_google",
                     "fifo_acquisition_google"):
            with self.subTest(program=name):
                self.assertIn(name, tdd_gate.EXPECTED_CTEST_NAMES)

    def preparation(self):
        modules = [{"path": path, "commit": "a" * 40} for path in (
            "vendors/st/cmsis_device_h7", "vendors/st/cmsis_device_u5",
            "vendors/st/stm32f4_hal", "ext/googletest", "ext/new-service")]
        return {"schema_version": 1, "kind": "dependency_preparation",
                "scope": "native", "status": "verified",
                "network_authorized": False, "backend": "baremetal",
                "family": None, "components": [], "host_support": False,
                "submodules": modules,
                "recursive_submodules": copy.deepcopy(modules)}

    def test_source_identity_projects_the_sdk_native_closure(self):
        report = self.preparation()
        expected = [{"path": row["path"], "revision": row["commit"]}
                    for row in report["recursive_submodules"]]
        self.assertEqual(repository.prepared_submodules(report), expected)

    def test_source_identity_rejects_unverified_preparation(self):
        for key, value in (("schema_version", True), ("scope", "arm"),
                           ("status", "not_assessed"),
                           ("network_authorized", True), ("kind", "other"),
                           ("host_support", True), ("host_support", 0),
                           ("components", ["network"]),
                           ("components", None), ("family", "stm32f4"),
                           ("unexpected", False)):
            report = self.preparation()
            report[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                repository.prepared_submodules(report)

    def test_source_identity_rejects_inconsistent_recursive_sources(self):
        for change in ("duplicate", "traversal", "short", "missing", "mismatch"):
            report = self.preparation()
            if change == "duplicate":
                report["recursive_submodules"].append(
                    copy.deepcopy(report["recursive_submodules"][0]))
            elif change == "traversal":
                report["recursive_submodules"][0]["path"] = "../escape"
            elif change == "short":
                report["recursive_submodules"][0]["commit"] = "aaaa"
            elif change == "missing":
                report["recursive_submodules"].pop()
            else:
                report["recursive_submodules"][0]["commit"] = "b" * 40
            with self.subTest(change=change), self.assertRaises(ValueError):
                repository.prepared_submodules(report)

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
        for family, (board, clock, binding, rx_format) in REFERENCE_BOARDS.items():
            for backend in ("baremetal", "freertos"):
                name = f"stm32-{family}-{backend}"
                with self.subTest(preset=name):
                    self.assertIn(name, presets)
                    preset = dict(presets.get(presets[name].get("inherits", ""), {}))
                    preset.update(presets[name])
                    self.assertEqual(preset["binaryDir"].replace("${presetName}", name),
                                     "${sourceDir}/build/" + name)
                    self.assertEqual(builds[name]["configurePreset"], name)
                    assembly = presets[name]["cacheVariables"]["NEXUS_ASSEMBLY_FILE"]
                    data = tomllib.loads(Path(assembly.replace(
                        "${sourceDir}", str(ROOT))).read_text())
                    self.assertEqual(data["board"], board)
                    self.assertEqual(data["clock"], clock)
                    self.assertEqual(data["backend"], backend)
                    self.assertEqual(set(data["gpio"]), {"status"})
                    self.assertEqual(data["gpio"]["status"]["binding"], "led0")
                    self.assertEqual(set(data["uart"]), {"console"})
                    uart = data["uart"]["console"]
                    self.assertEqual(uart["binding"], binding)
                    self.assertEqual(uart["mode"], "irq")
                    self.assertEqual(uart["baud"], 115200)
                    self.assertEqual(uart["rx"], {"format": rx_format, "capacity": 32})
                    self.assertEqual(uart["irq"]["priority"], 5)
                    self.assertEqual(uart["irq"].get("calls_os", False),
                                     rx_format == "events" and backend == "freertos")
                    self.assertEqual(data["components"], ["uart-owner"])
                    self.assertEqual(data["memory"]["libc_heap_bytes"], 0)
                    self.assertNotIn("dma", uart)

    def test_ci_builds_every_arm_preset_and_retains_reference_google_evidence(self):
        workflow = yaml.load((ROOT / ".github/workflows/ci.yml").read_text(),
                             Loader=yaml.BaseLoader)
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        expected = {entry["name"] for entry in presets["configurePresets"]
                    if not entry.get("hidden") and entry["name"] != "native-debug"}
        matrix = workflow["jobs"]["arm"]["strategy"]["matrix"]["preset"]
        self.assertEqual(matrix, "${{ fromJSON(needs.native.outputs.arm_presets) }}")
        self.assertEqual(expected, set(repository.REFERENCE_PRESETS) - {"native-debug"})
        paths = next(step["with"]["path"]
                     for step in workflow["jobs"]["native"]["steps"]
                     if step.get("name") == "Retain Native artifacts and execution evidence")
        self.assertIn("build/native-debug/tests/*execution/", paths)



if __name__ == "__main__":
    unittest.main()
