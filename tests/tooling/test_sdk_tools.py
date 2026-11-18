"""Exercise SDK imports in ordinary Python without modifying a real SDK."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
RUNNER = """
import json
from pathlib import Path
import sys

assert sys.dont_write_bytecode is False, "fixture requires ordinary Python"
sys.path.insert(0, sys.argv[1])
from sdk_tools import load, maintenance
source = Path(sys.argv[2])
mode = sys.argv[3]
sys.path.insert(0, str(source))
if mode == "load":
    module = load(source, "tools/dev/api.py", "selected_sdk_api")
    assert Path(module.__file__) == source / "tools/dev/api.py"
    assert module.IDENTITY == "selected SDK"
    assert module.later() == str(source / "tools/dev/later.py")
elif mode == "maintenance":
    lock, process = maintenance(source)
    assert Path(lock.__file__) == source / "tools/dev/build_lock.py"
    assert Path(process.__file__) == source / "tools/dev/process.py"
    assert lock.IDENTITY == process.IDENTITY == "selected SDK"
    assert process.later() == str(source / "tools/dev/later.py")
elif mode == "missing":
    try:
        load(source, "tools/dev/missing.py", "missing_sdk_api")
    except ValueError as error:
        assert str(source / "tools/dev/missing.py") in str(error)
    else:
        raise AssertionError("missing API must reject")
elif mode in {"load-error", "maintenance-error"}:
    try:
        if mode == "load-error":
            load(source, "tools/dev/broken.py", "broken_sdk_api")
        else:
            maintenance(source)
    except ModuleNotFoundError as error:
        assert error.name == "nexus_fixture_missing_dependency"
    else:
        raise AssertionError("real import failure must propagate")
print(json.dumps({"bytecode_disabled": sys.dont_write_bytecode}))
"""


class SDKImportSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="readonly SDK fixture ")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "selected SDK"
        dev = self.source / "tools/dev"
        dev.mkdir(parents=True)
        (self.source / "CMakeLists.txt").write_text("# import fixture only\n")
        for path in (self.source / "tools/__init__.py", dev / "__init__.py"):
            path.write_text("")
        content = (
            'from tools.dev import early\n'
            'IDENTITY = "selected SDK"\n'
            'def later():\n'
            '    from tools.dev import later\n'
            '    return later.__file__\n')
        for name in ("api.py", "build_lock.py", "process.py"):
            (dev / name).write_text(content)
        for name in ("early.py", "later.py"):
            (dev / name).write_text('IDENTITY = "selected SDK"\n')
        (dev / "broken.py").write_text("import nexus_fixture_missing_dependency\n")
        self.git("init", "--quiet")
        self.git("add", ".")
        self.git("-c", "user.name=SDK fixture", "-c",
                 "user.email=fixture@example.invalid", "commit", "--quiet",
                 "-m", "fixture source identity")

    def git(self, *arguments):
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("GIT_")}
        result = subprocess.run(["git", *arguments], cwd=self.source,
                                env=environment,
                                text=True, capture_output=True, check=True,
                                timeout=30)
        return result.stdout

    def execute(self, mode):
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("GIT_") and key not in {
                           "PYTHONDONTWRITEBYTECODE", "PYTHONPYCACHEPREFIX",
                           "PYTHONOPTIMIZE"}}
        result = subprocess.run([sys.executable, "-c", RUNNER, str(SCRIPTS),
                                 str(self.source), mode], env=environment,
                                text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.git("status", "--porcelain", "--untracked-files=all"),
                         "", "SDK imports changed its source identity")
        self.assertEqual(list(self.source.rglob("__pycache__")), [])
        self.assertTrue(json.loads(result.stdout)["bytecode_disabled"])

    def test_load_keeps_later_imports_out_of_source_identity(self):
        self.execute("load")

    def test_maintenance_keeps_later_imports_out_of_source_identity(self):
        self.execute("maintenance")

    def test_readonly_sdk_supports_load_and_later_maintenance_imports(self):
        paths = [path for path in self.source.rglob("*")
                 if ".git" not in path.relative_to(self.source).parts]
        self.addCleanup(self.restore_permissions, paths)
        for path in paths:
            path.chmod(0o555 if path.is_dir() else 0o444)
        self.execute("load")
        self.execute("maintenance")

    @staticmethod
    def restore_permissions(paths):
        for path in paths:
            path.chmod(0o755 if path.is_dir() else 0o644)

    def test_missing_api_keeps_the_selected_path(self):
        self.execute("missing")

    def test_load_preserves_import_errors(self):
        self.execute("load-error")

    def test_maintenance_preserves_import_errors(self):
        (self.source / "tools/dev/process.py").write_text(
            "import nexus_fixture_missing_dependency\n")
        self.git("add", ".")
        self.git("-c", "user.name=SDK fixture", "-c",
                 "user.email=fixture@example.invalid", "commit", "--quiet",
                 "-m", "fixture import error")
        self.execute("maintenance-error")


if __name__ == "__main__":
    unittest.main()
