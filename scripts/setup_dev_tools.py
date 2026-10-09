#!/usr/bin/env python3
"""Install SDK-locked tools and this repository's two nonmutating Git hooks."""

import argparse
from pathlib import Path
import subprocess
import sys

from sdk_tools import ROOT, load, sdk_root


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nexus-source", type=Path)
    parser.add_argument("--skip-hooks", action="store_true")
    args = parser.parse_args(argv)
    try:
        source = sdk_root(args.nexus_source)
        installer = load(source, "scripts/setup/install_dev_tools.py", "example_sdk_setup")
        pins = installer.read_lock(source / installer.LOCK_NAME)
        if not args.skip_hooks:
            installer.check_hook_location(ROOT)
        directory, python, formatter = installer.ensure_venv(ROOT)
        installed = installer.inspect_venv(directory, python, ROOT)
        if installed != pins:
            installer.run([python, "-m", "pip", "install", "-r",
                           source / installer.LOCK_NAME], ROOT)
        if installer.inspect_venv(directory, python, ROOT) != pins:
            raise ValueError("Installed tool versions differ from source SDK lock")
        installer.verify_tools(python, formatter, pins, ROOT)
        if not args.skip_hooks:
            installer.run([python, "-I", "-m", "pre_commit", "install", "--install-hooks",
                           "--hook-type", "pre-commit", "--hook-type", "commit-msg"], ROOT)
        print(f"SDK-locked tools verified in {directory}; hooks "
              + ("skipped" if args.skip_hooks else "installed with existing-hook migration"))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Example development setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
