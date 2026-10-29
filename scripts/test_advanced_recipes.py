"""Advanced recipes own exact TOML and depend only on public platform faces."""
from pathlib import Path
import tomllib
import unittest

from sdk_tools import ROOT


class AdvancedRecipeContracts(unittest.TestCase):
    def test_cache_on_receive_keeps_whole_reservations_and_explicit_clock(self):
        project = ROOT / "apps/stream_recovery/h743_cache_on"
        assembly = tomllib.loads((project / "assembly.toml").read_text())
        self.assertEqual(assembly["board"], "stm32h743zi_hsi_reference")
        self.assertEqual(assembly["clock"], "hsi64-fixed-cache-on")
        self.assertEqual(assembly["backend"], "baremetal")
        self.assertEqual(assembly["uart"]["console"]["mode"],
                         "rx-dma-continuous")
        self.assertEqual(assembly["uart"]["console"]["rx"]["format"], "blocks")
        self.assertIn("../h743/main.c", (project / "CMakeLists.txt").read_text())

    def test_pause_recipe_has_one_explicit_controller_and_no_hidden_scheduler(self):
        project = ROOT / "apps/runtime_pause/h743"
        assembly = tomllib.loads((project / "assembly.toml").read_text())
        self.assertEqual(assembly["backend"], "baremetal")
        self.assertEqual(assembly["clock"], "hsi64-fixed-cache-off")
        self.assertEqual(assembly["power"],
                         {"mode": "fixed-run", "controllers": ["bus4"]})
        self.assertEqual(set(assembly["spi"]), {"bus4"})
        self.assertEqual(assembly["spi"]["bus4"]["mode"], "dma")
        self.assertNotIn("os", assembly)
        self.assertIn("main.c", (project / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
