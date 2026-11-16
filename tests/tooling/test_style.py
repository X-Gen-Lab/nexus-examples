#!/usr/bin/env python3
"""Exercise real SDK formatter/comment checks and owned-file boundaries."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_style
from sdk_tools import ROOT, sdk_root


class StyleIntegrationTest(unittest.TestCase):
    def test_real_formatter_and_comment_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "main.c"
            source.write_bytes((ROOT / "apps/platform_reference/main.c").read_bytes())
            with patch.object(check_style, "ROOT", root):
                self.assertEqual(check_style.check(sdk_root(), [source])["status"], "passed")
                source.write_bytes(source.read_bytes().replace(
                    b"int main(void)", b"int  main( void )", 1) + b"// bad comment\n")
                report = check_style.check(sdk_root(), [source])
            self.assertEqual(report["status"], "failed")
            self.assertTrue(any("clang-format differs" in item for item in report["issues"]))
            self.assertTrue(any("instead of //" in item for item in report["issues"]))

    def test_selection_rejects_escape_and_symlink_but_excludes_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "main.c"
            source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
            (root / "external").mkdir()
            (root / "external/vendor.c").write_text("vendor\n", encoding="utf-8")
            self.assertEqual(check_style.select_files(root, ["main.c", "external/vendor.c"]), [source])
            with self.assertRaises(ValueError):
                check_style.select_files(root, ["../escaped.c"])
            (root / "linked.c").symlink_to(source)
            with self.assertRaises(ValueError):
                check_style.select_files(root, ["linked.c"])

    def test_owned_toml_configuration_obeys_text_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            assembly = root / "native.toml"
            assembly.write_bytes(b'schema = 2  \r\n')
            selected = check_style.select_files(root, ["native.toml"])
            self.assertEqual(selected, [assembly])
            with patch.object(check_style, "ROOT", root):
                report = check_style.check(sdk_root(), selected)
            self.assertEqual(report["status"], "failed")
            self.assertTrue(any("require LF" in issue for issue in report["issues"]))


if __name__ == "__main__":
    unittest.main()
