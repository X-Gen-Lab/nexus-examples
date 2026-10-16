#!/usr/bin/env python3
"""Qualify the staged example snapshot with its clean pinned SDK and real tests."""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from run_google import identities
from sdk_tools import ROOT, load


RELEVANT_PREFIXES = ("apps/", "reproduction/", "assemblies/", "tests/", "scripts/", "dependencies/")
RELEVANT_FILES = {"CMakeLists.txt", "CMakePresets.json", ".pre-commit-config.yaml",
                  ".gitmodules", "external/nexus", "AGENTS.md"}
EXPECTED_CTEST_NAMES = (
    "uart_owner_shutdown", "platform_static_ports", "sensor_explicit_spi",
    "persistent_journal", "protocol_authorized_registers", "owner_lifecycle_google",
    "product_fault_policy_google", "reference_rx_policy_google",
    "reference_lifecycle_google",
)


def relevant(name):
    """Select all owned behavior and its formal SDK identity inputs."""
    return name in RELEVANT_FILES or name.startswith(RELEVANT_PREFIXES)


def git(root, *arguments):
    """Keep the original hook index environment for staged-source inspection."""
    return subprocess.run(["git", *arguments], cwd=root, capture_output=True,
                          text=True, check=True).stdout


def staged_tree(root):
    return git(root, "write-tree").strip()


def check_gitlink(root, identity):
    """Bind the original hook index, including an explicit alternate index."""
    expected = "160000 " + identity["nexus_revision"] + " 0\texternal/nexus\n"
    if git(root, "ls-files", "--stage", "--", "external/nexus") != expected:
        raise ValueError("Hook index Gitlink differs from the verified formal SDK")


def check_snapshot(root):
    """Never use a worktree-only fix to certify a different committed source."""
    dirty = []
    for arguments in (("diff", "--name-only", "-z"),
                      ("ls-files", "--others", "--exclude-standard", "-z")):
        dirty.extend(name for name in git(root, *arguments).split("\0")
                     if name and relevant(name))
    if dirty:
        raise ValueError("Unstaged behavioral changes differ from the commit: " +
                         ", ".join(sorted(set(dirty))))


def child_environment(root, environ):
    """Child SDK commands use their own Git checkout, without hook filters."""
    if environ.get("SKIP") or environ.get("NEXUS_SKIP_TDD"):
        raise ValueError("Hook bypass settings cannot establish TDD evidence")
    # A source SDK is a different checkout. Hook GIT_DIR/GIT_INDEX_FILE must
    # never redirect its identity check into the examples commit index.
    result = {key: value for key, value in environ.items()
              if not key.startswith(("GIT_", "GTEST_"))}
    result["NEXUS_SOURCE"] = str(root / "external/nexus")
    return result


def execute(root, arguments, environment, log):
    """Retain actual commands, exit codes and output without auto staging."""
    command = list(map(str, arguments))
    print("+ " + shlex.join(command), flush=True)
    result = subprocess.run(command, cwd=root, env=environment, text=True,
                            capture_output=True, check=False)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    log.with_suffix(".command.json").write_text(json.dumps({
        "schema_version": 1, "argv": command, "cwd": str(root),
        "exit_code": result.returncode,
    }, indent=2) + "\n", encoding="utf-8")
    if result.returncode:
        raise ValueError(f"Command failed ({result.returncode}); see {log}")
    return result.stdout


def safe_outputs(root, output):
    """Generated output must not traverse a symlink into retained source."""
    paths = (output, output / "execution.json", output / "source.json",
             output / "source-after.json", output / "source-after.log",
             output / "source-after.command.json",
             output / "source.log", output / "source.command.json",
             output / "tools.log", output / "tools.command.json",
             output / "build.log", output / "build.command.json",
             output / "catalogue.log", output / "catalogue.command.json",
             root / "build/native-debug/ctest-results.xml",
             root / "build/native-debug/build.log",
             root / "build/native-debug/tests/google-execution/google-results.xml")
    for path in paths:
        if any(part.is_symlink() for part in (path, *path.parents)):
            raise ValueError("TDD output must not traverse a symlink: " + str(path))


def run_gate(root, *, paths=(), staged=False, force=False, environ=None):
    """Require complete fresh execution for the exact index and formal SDK."""
    root = Path(root).resolve()
    names = set(paths)
    names.update(git(root, "diff", "--cached", "--name-only", "-z").split("\0"))
    if staged and not force and not any(map(relevant, names)):
        print("No staged behavioral changes require example execution")
        return None
    initial = staged_tree(root)
    check_snapshot(root)
    env = child_environment(root, os.environ if environ is None else environ)
    source = root / "external/nexus"
    build = root / "build/native-debug"
    output = build / "tdd"
    safe_outputs(root, output)
    output.mkdir(parents=True, exist_ok=True)
    summary = output / "execution.json"
    summary.unlink(missing_ok=True)
    source_report = output / "source.json"
    source_report.unlink(missing_ok=True)
    python = sys.executable
    execute(root, [python, "scripts/test_repository.py", "--report",
                   str(source_report)], env, output / "source.log")
    identity = json.loads(source_report.read_text(encoding="utf-8"))
    if (identity.get("kind") != "example-source-dependency-identity" or
            identity.get("candidate_validation") is not False or
            identity.get("source_revision_matches_lock") is not True or
            identity.get("example_gitlink_verified") is not True):
        raise ValueError("A clean formal SDK Gitlink/lock identity is required")
    check_gitlink(root, identity)
    validator = load(source, "scripts/ci/tdd_gate.py", "example_tdd_sdk_validator")
    execute(root, [python, "-B", "-m", "unittest", "discover", "-s", "scripts",
                   "-p", "test_*.py", "-v"], env, output / "tools.log")
    ctest_report = build / "ctest-results.xml"
    ctest_report.unlink(missing_ok=True)
    google_output = build / "tests/google-execution"
    google_report = google_output / "google-results.xml"
    if google_report.is_symlink():
        raise ValueError("GoogleTest evidence must not be a symlink")
    google_report.unlink(missing_ok=True)
    started = time.time_ns()
    execute(root, [python, "scripts/build.py", "--preset", "native-debug",
                   "--test", "--parallel", "2"], env, output / "build.log")
    catalogue = json.loads(execute(root, ["ctest", "--test-dir", str(build),
                                        "--show-only=json-v1"], env,
                                   output / "catalogue.log"))
    discovered = [entry["name"] for entry in catalogue["tests"]]
    if (len(discovered) != len(set(discovered)) or
            not set(EXPECTED_CTEST_NAMES).issubset(discovered)):
        raise ValueError("Maintained Native CTest catalogue is missing or repeated")
    ctest = validator.validate_junit(ctest_report, not_before_ns=started)
    actual = {case["name"] for case in ctest["cases"]}
    if ctest["skipped"] or actual != set(discovered):
        raise ValueError("Native CTest report does not execute every discovered test")
    discovery = google_output / "discovery.log"
    if (not discovery.is_file() or discovery.is_symlink() or
            discovery.stat().st_mtime_ns < started):
        raise ValueError("GoogleTest discovery is missing or stale")
    listing = discovery.read_text(encoding="utf-8")
    count = validator.discovered_count(listing)
    google = validator.validate_google_report(google_report, not_before_ns=started,
                                              expected_cases=count)
    actual_google = {(case.get("classname"), case.get("name"))
                     for case in ET.parse(google_report).getroot().iter("testcase")}
    if actual_google != identities(listing):
        raise ValueError("GoogleTest identities differ from complete discovery")
    final_source = output / "source-after.json"
    final_source.unlink(missing_ok=True)
    execute(root, [python, "scripts/test_repository.py", "--report",
                   str(final_source)], env, output / "source-after.log")
    if json.loads(final_source.read_text(encoding="utf-8")) != identity:
        raise ValueError("Formal SDK identity changed during example execution")
    check_gitlink(root, identity)
    check_snapshot(root)
    if staged_tree(root) != initial:
        raise ValueError("Staged index changed during example execution")
    report = {
        "schema_version": 1, "kind": "example-development-contract-execution",
        "index_tree": initial, "preset": "native-debug", "source": identity,
        "ctest": ctest, "google": google, "hardware_verified": False,
        "scope": "Fresh exact-index host execution; historical TDD order and "
                 "physical hardware qualification require separate evidence",
    }
    summary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true")
    parser.add_argument("--all", action="store_true", help="also execute docs-only snapshots")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args(argv)
    try:
        report = run_gate(ROOT, paths=args.paths, staged=args.staged, force=args.all)
        if report:
            print("Executed complete Native CTest and " +
                  str(report["google"]["passed"]) + " GoogleTest cases")
        return 0
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError,
            subprocess.SubprocessError) as error:
        print("Example TDD gate rejected: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
