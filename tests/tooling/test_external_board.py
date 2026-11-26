"""Exercise product-owned Board onboarding through the actual SDK CLI."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from sdk_tools import ROOT, sdk_root


class ExternalBoardContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="private Board product ")
        self.addCleanup(self.temporary.cleanup)
        self.product = Path(self.temporary.name)
        self.board = self.product / "boards/software-fixture"
        shutil.copytree(ROOT / "apps/shared_spi_runtime/software-board", self.board)
        self.assembly = self.product / "platform.toml"
        self.cli = sdk_root() / "tools/configure/configure.py"
        self.run_cli("init", "--board-package", self.board, "--backend", "baremetal",
                     "--clock", "hse8-pll168", "--profile", "gpio", "--binding", "primary-cs",
                     "--instance", "select", "--output", self.assembly)

    def run_cli(self, *arguments, success=True):
        result = subprocess.run([sys.executable, "-B", str(self.cli), *map(str, arguments)],
                                cwd=self.product, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0 if success else 1,
                         result.stdout + result.stderr)
        return result

    def test_private_package_remains_relative_and_uses_exact_provider_facts(self):
        self.assertIn('board_package = "boards/software-fixture"', self.assembly.read_text())
        report = json.loads(self.run_cli("provider-check", "--assembly", self.assembly,
                            "--expect-family", "stm32f4", "--expect-part", "STM32F407ZGT6",
                            "--json").stdout)
        self.assertEqual(report["part"], "STM32F407ZGT6")
        self.assertEqual(report["controllers"][0]["controller"], "GPIOE")
        self.assertIn("nexus_factory_strong.h", report["generated_files"])
        self.assertIn("bindings.c", report["generated_files"])
        self.assertIn("external/board.json", {item["path"] for item in report["fact_inputs"]})
        self.assertEqual(report["physical_verification"], "not_established_by_configuration")
        rejection = self.run_cli("provider-check", "--assembly", self.assembly,
                                 "--expect-part", "STM32F407VET6", "--diagnostics-json",
                                 success=False)
        self.assertIn("part", json.loads(rejection.stderr)["message"].lower())

    def test_product_diff_explains_identity_storage_and_execution_obligations(self):
        changed = self.product / "changed.toml"
        changed.write_text(self.assembly.read_text().replace("[gpio.select]", "[gpio.output]")
                           .replace("2048", "4096"))
        delta = json.loads(self.run_cli("diff", "--assembly", self.assembly,
                                       "--against", changed, "--json").stdout)
        categories = {item["category"] for item in delta["impacts"]}
        self.assertTrue({"factory_ids", "storage", "public_interface", "execution"} <= categories)
        execution = [item["obligation"] for item in delta["impacts"]
                     if item["category"] == "execution"]
        self.assertTrue(any("stop" in message and "borrowed" in message for message in execution))
        invalid = self.product / "invalid.toml"
        invalid.write_text(self.assembly.read_text().replace('mode = "output"',
                                                            'mode = "output"\nmodde = "input"'))
        error = self.run_cli("check", "--assembly", invalid, "--diagnostics-json", success=False)
        diagnostic = json.loads(error.stderr)
        self.assertIn("modde", json.dumps(diagnostic))
        self.assertIn(str(invalid), json.dumps(diagnostic))


    def test_usb_standalone_resolves_its_f4_provider_and_strong_roles(self):
        output = self.product / "usb bundle"
        self.run_cli("--assembly", ROOT / "apps/usb_configuration/assembly.toml",
                     "--output", output)
        resolved = json.loads((output / "resolved.json").read_text())
        self.assertEqual(resolved["soc_family"], "stm32f4")
        self.assertEqual(resolved["part"], "STM32F407ZGT6")
        self.assertFalse(resolved["physical_qualified"])
        header = (output / "nexus_factory_strong.h").read_text()
        self.assertIn("NX_USB_DEVICE_REF_FS", header)
        self.assertIn("nx_factory_usb_device_ref", header)
        extensions = (output / "nexus_factory.h").read_text()
        self.assertIn("nx_factory_usb_profile(", extensions)
        self.assertIn("nx_factory_usb_activation(", extensions)

    def test_managed_gateway_resolves_its_owned_software_reference(self):
        output = self.product / "managed gateway bundle"
        assembly = ROOT / "apps/network_gateway/managed-f407/assembly.toml"
        self.run_cli("--assembly", assembly, "--output", output)
        resolved = json.loads((output / "resolved.json").read_text())
        self.assertEqual(resolved["board"],
                         "qiming-f407zg-managed-software-reference")
        self.assertEqual(resolved["part"], "STM32F407ZGT6")
        self.assertEqual(resolved["soc_family"], "stm32f4")
        self.assertIsNone(resolved["physical_pcb_revision"])
        self.assertFalse(resolved["physical_qualified"])
        self.assertEqual({item["id"]: item["controller"]
                          for item in resolved["controllers"]},
                         {"mac": "ETH", "random": "RNG"})
        self.assertIn("external/board.json",
                      {item["path"] for item in resolved["inputs"]})


if __name__ == "__main__":
    unittest.main()
