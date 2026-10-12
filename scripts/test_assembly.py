"""Resolve external TOML examples with the actual selected source SDK."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from sdk_tools import ROOT, sdk_root


class ExampleAssemblyTests(unittest.TestCase):
    def test_every_visible_preset_selects_one_resolvable_toml_input(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        visible = [entry for entry in presets["configurePresets"]
                   if not entry.get("hidden")]
        self.assertEqual(len(visible), 11)
        source = sdk_root()
        with tempfile.TemporaryDirectory(prefix="example TOML resolution ") as value:
            directory = Path(value)
            for preset in visible:
                assembly = Path(preset["cacheVariables"]["NEXUS_ASSEMBLY_FILE"]
                                .replace("${sourceDir}", str(ROOT)))
                with self.subTest(preset=preset["name"]):
                    self.assertEqual(assembly.suffix, ".toml")
                    output = directory / preset["name"]
                    result = subprocess.run(
                        [sys.executable, "-B", str(source / "tools/configure/configure.py"),
                         "--assembly", str(assembly), "--output", str(output)],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    resolved = json.loads((output / "resolved.json").read_text())
                    self.assertEqual(len([item for item in resolved["controllers"]
                                          if item["kind"] == "uart"]), 1)
                    header = (output / "nexus_factory.h").read_text()
                    self.assertIn("NX_GPIO_ID_LED0", header)
                    self.assertIn("nx_factory_uart", header)
                    if preset["name"] == "native-debug":
                        self.assertIn("NX_SPI_DEVICE_ID_SENSOR0", header)


if __name__ == "__main__":
    unittest.main()
