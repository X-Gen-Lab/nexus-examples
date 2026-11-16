#!/usr/bin/env python3
"""Check example-owned text and C files with the source SDK's exact style.

Git hooks use pre-commit's staged-file isolation; manual invocation checks the
working tree. No command rewrites or stages files. --all selects tracked files.
The SDK's formatter and mechanical comment API are the single C policy source.
This check does not establish source identity or physical qualification.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from sdk_tools import ROOT, load, sdk_root


EXCLUDED = {"external", "build", "out", "dist", ".venv", ".git", ".tools"}
C_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}
TEXT_SUFFIXES = C_SUFFIXES | {".py", ".json", ".toml", ".md", ".cmake", ".txt", ".yml", ".yaml"}
TEXT_NAMES = {"CMakeLists.txt", ".gitignore", ".gitmodules", ".editorconfig"}


def select_files(root, names):
    paths = set()
    for name in names:
        path = root / name
        if path.is_absolute() and not path.absolute().is_relative_to(root):
            raise ValueError(f"Path outside the example repository: {name}")
        relative = path.absolute().relative_to(root)
        if any(part in EXCLUDED for part in relative.parts):
            continue
        if path.is_symlink() or any(parent.is_symlink() for parent in path.parents
                                    if parent.is_relative_to(root)):
            raise ValueError(f"Owned file traverses a symlink: {name}")
        if not path.resolve().is_relative_to(root):
            raise ValueError(f"Path escapes the example repository: {name}")
        if not path.exists():
            continue
        if not path.is_file():
            raise ValueError(f"Owned input is not a file: {name}")
        if path.suffix in TEXT_SUFFIXES or path.name in TEXT_NAMES:
            paths.add(path.resolve())
    return sorted(paths)


def check(source, paths):
    formatter = load(source, "scripts/tools/format.py", "example_sdk_format")
    comments = load(source, "scripts/ci/comment_style.py", "example_sdk_comments")
    installer = load(source, "scripts/setup/install_dev_tools.py", "example_sdk_installer")
    pins = installer.read_lock(source / "dependencies/development-tools.txt")
    tool = formatter.default_tool(ROOT)
    if tool == "clang-format":
        tool = formatter.default_tool(source)
    tool, version = formatter.check_tool(tool, source / ".clang-format")
    match = re.search(r"clang-format version\s+([0-9]+\.[0-9]+\.[0-9]+)", version)
    if not match or match.group(1) != pins["clang-format"]:
        raise ValueError(f"Formatter must match SDK lock {pins['clang-format']}: {version}")
    issues, checked = [], []
    for path in paths:
        name = path.relative_to(ROOT).as_posix()
        contents = path.read_bytes()
        checked.append({"path": name, "sha256": hashlib.sha256(contents).hexdigest()})
        try:
            text = contents.decode("utf-8")
        except UnicodeError:
            issues.append(f"{name}: text must be UTF-8")
            continue
        if "\r" in text or (text and not text.endswith("\n")):
            issues.append(f"{name}: require LF and final newline")
        if path.suffix != ".md" and any(line.endswith((" ", "\t")) for line in text.splitlines()):
            issues.append(f"{name}: trailing whitespace")
        if path.suffix == ".py":
            try:
                compile(text, name, "exec")
            except SyntaxError as error:
                issues.append(f"{name}:{error.lineno}: Python syntax: {error.msg}")
        if path.suffix in C_SUFFIXES:
            if formatter.formatted_source(path, tool, source / ".clang-format") != contents:
                issues.append(f"{name}: clang-format differs; use the SDK .clang-format")
            for item in comments.inspect_source(name, contents)["diagnostics"]:
                issues.append(f"{name}:{item['line']}: {item['message']}")
    return {"schema_version": 1, "kind": "example_style_check",
            "status": "failed" if issues else "passed", "formatter": version,
            "style_sha256": hashlib.sha256((source / ".clang-format").read_bytes()).hexdigest(),
            "files": checked, "issues": issues,
            "source_identity_verified": False, "hardware_verified": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--nexus-source", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    if args.all and args.files:
        parser.error("choose --all or explicit files")
    try:
        if args.report:
            args.report.unlink(missing_ok=True)
        names = args.files
        if args.all:
            result = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                                    capture_output=True, check=True)
            names = result.stdout.decode("utf-8").rstrip("\0").split("\0")
        paths = select_files(ROOT, names)
        if args.all and not any(path.suffix in C_SUFFIXES for path in paths):
            raise ValueError("full check requires tracked example C sources")
        report = check(sdk_root(args.nexus_source), paths)
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        for issue in report["issues"]:
            print(issue, file=sys.stderr)
        print(f"Example style: {report['status']}; {len(paths)} files")
        return 0 if report["status"] == "passed" else 1
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Example style preflight failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
