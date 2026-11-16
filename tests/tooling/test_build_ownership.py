"""Exercise actual kernel exclusion and bounded child withdrawal."""

import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import build
from sdk_tools import sdk_root


class BuildOwnershipTests(unittest.TestCase):
    def test_competing_owner_rejects_before_replacing_the_build_log(self):
        with tempfile.TemporaryDirectory(prefix="competing example build ") as value:
            root = Path(value)
            sdk = sdk_root()
            directory = root / "build/native-debug"
            directory.mkdir(parents=True)
            log = directory / "build.log"
            log.write_text("retained earlier output\n")
            code = ("import sys; sys.path.insert(0, sys.argv[1]); "
                    "from tools.dev.build_lock import build_lock; "
                    "lease=build_lock(sys.argv[2]); lease.__enter__(); "
                    "print('locked',flush=True); sys.stdin.read()")
            owner = subprocess.Popen([sys.executable, "-c", code, str(sdk), str(directory)],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
            try:
                self.assertEqual(owner.stdout.readline().strip(), "locked")
                with mock.patch.object(build, "ROOT", root), \
                        mock.patch.object(build, "run", return_value=0) as command:
                    result = build.main(["--preset", "native-debug",
                                         "--nexus-source", str(sdk)])
                self.assertNotEqual(result, 0)
                command.assert_not_called()
                self.assertEqual(log.read_text(), "retained earlier output\n")
            finally:
                owner.communicate("", timeout=5)

    def test_managed_child_receives_the_real_parent_lease(self):
        with tempfile.TemporaryDirectory(prefix="delegated example build ") as value:
            root = Path(value)
            sdk = sdk_root()
            directory = root / "build/native-debug"
            from sdk_tools import maintenance
            lock, process = maintenance(sdk)
            program = "\n".join([
                "import sys; from pathlib import Path; from unittest import mock",
                "sys.path.insert(0," + repr(str(Path(build.__file__).parent)) + ")",
                "import build",
                "build.ROOT=Path(" + repr(str(root)) + ")",
                "with mock.patch.object(build,'run',return_value=0):",
                "    raise SystemExit(build.main(['--preset','native-debug','--nexus-source'," + repr(str(sdk)) + "]))",
            ])
            with lock.build_lock(directory):
                result = process.run([sys.executable, "-c", program], cwd=root,
                                     env=os.environ.copy(), timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_timeout_withdraws_children_and_retains_actual_output(self):
        with tempfile.TemporaryDirectory(prefix="bounded example child ") as value:
            root = Path(value)
            marker = root / "escaped.txt"
            child = ("import pathlib,time; time.sleep(0.8); "
                     "pathlib.Path(" + repr(str(marker)) + ").write_text('unexpected')")
            program = ("import subprocess,sys,time; "
                       "subprocess.Popen([sys.executable,'-c'," + repr(child) + "]); "
                       "print('before timeout',flush=True); time.sleep(1.5)")
            environment = dict(os.environ, NEXUS_COMMAND_TIMEOUT="0.15",
                               NEXUS_SOURCE=str(sdk_root()))
            log = io.StringIO()
            started = time.monotonic()
            with mock.patch.object(build, "ROOT", root):
                result = build.run([sys.executable, "-c", program], environment, log)
            self.assertEqual(result, 124)
            self.assertIn("before timeout", log.getvalue())
            # The deadline starts separately bounded freeze/join withdrawal.
            self.assertLess(time.monotonic() - started, 12.0)
            time.sleep(0.9)
            self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
