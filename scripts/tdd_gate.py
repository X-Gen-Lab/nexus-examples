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

from build import positive_integer
from run_google import identities
from sdk_tools import ROOT, load, maintenance


RELEVANT_PREFIXES = (".github/", "apps/", "configs/", "templates/", "tests/", "scripts/", "dependencies/")
RELEVANT_FILES = {"CMakeLists.txt", "CMakePresets.json", ".pre-commit-config.yaml",
                  ".gitmodules", ".taplo.toml", "external/nexus", "external/googletest", "AGENTS.md"}
EXPECTED_CTEST_NAMES = (
    "uart_owner_shutdown", "platform_static_ports", "sensor_explicit_spi",
    "persistent_journal", "protocol_authorized_registers", "owner_lifecycle_google",
    "product_fault_policy_google", "reference_rx_policy_google",
    "reference_lifecycle_google", "fifo_acquisition_google",
    "shared_spi_two_devices", "stream_slow_consumer", "developer_recipes_google",
    "continuous_rx_google",
    "industrial_control_reference", "industrial_control_google",
    "network_gateway_policy_google", "network_gateway_frames_google",
    "network_gateway_application_google", "network_gateway_peer_google",
    "network_gateway_main_google", "usb_configuration_google",
    "flash_maintenance_google",
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
    # Location is explicit, while verify_sdk still verifies the same
    # formal lock, staged Gitlink, tree, clean source and vendor dependencies.
    source = Path(result.get("NEXUS_SOURCE") or root / "external/nexus")
    result["NEXUS_SOURCE"] = str(source.expanduser().resolve())
    return result


def execute(root, arguments, environment, log, *, tool_source=None):
    """Retain actual commands, exit codes and output without auto staging."""
    command = list(map(str, arguments))
    print("+ " + shlex.join(command), flush=True)
    _, process = maintenance(tool_source or environment.get("NEXUS_SOURCE"))
    result = process.run(command, cwd=root, env=environment, log=log,
                         stage="example-ci")
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



def collect_google_programs(root, build, catalogue, validator, started):
    """Summarize every actual wrapper execution without running binaries again."""
    programs = []
    binaries, outputs = set(), set()
    wrapper = str(root / "scripts/run_google.py")
    for entry in catalogue["tests"]:
        properties = {item["name"]: item["value"]
                      for item in entry.get("properties", [])}
        command = entry.get("command", [])
        labelled = "GoogleTest" in properties.get("LABELS", [])
        wrapped = wrapper in command
        if labelled != wrapped:
            raise ValueError("GoogleTest label and execution wrapper differ: " +
                             entry["name"])
        if not labelled:
            continue
        working = Path(properties.get("WORKING_DIRECTORY", build))
        if not working.is_absolute():
            working = build / working
        arguments = {}
        for option in ("--binary", "--output"):
            if command.count(option) != 1:
                raise ValueError("GoogleTest wrapper requires one " + option)
            index = command.index(option) + 1
            if index == len(command):
                raise ValueError("GoogleTest wrapper argument is missing: " + option)
            path = Path(command[index])
            arguments[option] = path if path.is_absolute() else working / path
        binary = validator.checked_binary(arguments["--binary"], build).resolve()
        output = arguments["--output"]
        discovery, report = output / "discovery.log", output / "google-results.xml"
        for path in (output, discovery, report):
            if any(part.is_symlink() for part in (path, *path.parents)):
                raise ValueError("GoogleTest evidence must not traverse a symlink")
            if not path.resolve().is_relative_to(build):
                raise ValueError("GoogleTest evidence must remain in the selected build")
        output = output.resolve()
        if binary in binaries or output in outputs:
            raise ValueError("GoogleTest binary or output is repeated in CTest")
        binaries.add(binary)
        outputs.add(output)
        if not discovery.is_file() or discovery.stat().st_mtime_ns < started:
            raise ValueError("GoogleTest discovery is missing or stale: " + entry["name"])
        listing = discovery.read_text(encoding="utf-8")
        count = validator.discovered_count(listing)
        google = validator.validate_google_report(report, not_before_ns=started,
                                                  expected_cases=count)
        actual = {(case.get("classname"), case.get("name"))
                  for case in ET.parse(report).getroot().iter("testcase")}
        if actual != identities(listing):
            raise ValueError("GoogleTest identities differ from complete discovery: " +
                             entry["name"])
        programs.append({"ctest_name": entry["name"], "binary": str(binary),
                         "output": str(output), "google": google})
    if not programs:
        raise ValueError("CTest catalogue contains no GoogleTest programs")
    return programs


def run_gate(root, *, paths=(), staged=False, force=False, environ=None):
    """Keep the complete owned execution and reports under one kernel lease."""
    settings = os.environ if environ is None else environ
    environment = child_environment(Path(root).resolve(), settings)
    lock, _ = maintenance(environment["NEXUS_SOURCE"])
    with lock.build_lock(Path(root) / "build/native-debug"):
        return _run_gate(root, paths=paths, staged=staged, force=force,
                         environ=settings)


def _run_gate(root, *, paths=(), staged=False, force=False, environ=None):
    """Require complete fresh execution for the exact index and formal SDK."""
    settings = os.environ if environ is None else environ
    try:
        jobs = positive_integer(settings.get(
            "NEXUS_BUILD_JOBS", settings.get("CMAKE_BUILD_PARALLEL_LEVEL", "2")))
    except (argparse.ArgumentTypeError, TypeError, ValueError) as error:
        raise ValueError("TDD parallel jobs must be a positive integer") from error
    root = Path(root).resolve()
    names = set(paths)
    names.update(git(root, "diff", "--cached", "--name-only", "-z").split("\0"))
    if staged and not force and not any(map(relevant, names)):
        print("No staged behavioral changes require example execution")
        return None
    initial = staged_tree(root)
    check_snapshot(root)
    env = child_environment(root, settings)
    source = Path(env["NEXUS_SOURCE"])
    build = root / "build/native-debug"
    output = build / "tdd"
    safe_outputs(root, output)
    output.mkdir(parents=True, exist_ok=True)
    summary = output / "execution.json"
    summary.unlink(missing_ok=True)
    source_report = output / "source.json"
    source_report.unlink(missing_ok=True)
    python = sys.executable
    execute(root, [python, "scripts/verify_sdk.py", "--nexus-source", str(source), "--report",
                   str(source_report)], env, output / "source.log")
    identity = json.loads(source_report.read_text(encoding="utf-8"))
    if (identity.get("kind") != "example-source-dependency-identity" or
            identity.get("candidate_validation") is not False or
            identity.get("source_revision_matches_lock") is not True or
            identity.get("example_gitlink_verified") is not True):
        raise ValueError("A clean formal SDK Gitlink/lock identity is required")
    check_gitlink(root, identity)
    validator = load(source, "scripts/ci/tdd_gate.py", "example_tdd_sdk_validator")
    execute(root, [python, "-B", "scripts/run_tool_tests.py"], env, output / "tools.log")
    ctest_report = build / "ctest-results.xml"
    ctest_report.unlink(missing_ok=True)
    google_output = build / "tests/google-execution"
    google_report = google_output / "google-results.xml"
    if google_report.is_symlink():
        raise ValueError("GoogleTest evidence must not be a symlink")
    google_report.unlink(missing_ok=True)
    started = time.time_ns()
    execute(root, [python, "scripts/build.py", "--preset", "native-debug",
                   "--nexus-source", str(source), "--test", "--parallel", str(jobs)], env, output / "build.log")
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
    programs = collect_google_programs(root, build, catalogue, validator, started)
    # Preserve the original single-program field for existing report consumers.
    google = next((program["google"] for program in programs
                   if program["ctest_name"] == "owner_lifecycle_google"), None)
    if google is None:
        raise ValueError("Maintained owner_lifecycle_google wrapper is missing")
    aggregate = {field: sum(program["google"][field] for program in programs)
                 for field in ("tests", "passed", "failures", "errors", "skipped")}
    aggregate["programs"] = len(programs)
    final_source = output / "source-after.json"
    final_source.unlink(missing_ok=True)
    execute(root, [python, "scripts/verify_sdk.py", "--nexus-source", str(source), "--report",
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
        "ctest": ctest, "google": google,
        "google_scope": "owner_lifecycle_google (legacy single-program summary)",
        "google_programs": programs, "google_aggregate": aggregate,
        "hardware_verified": False,
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
                  str(report["google_aggregate"]["passed"]) + " GoogleTest cases in " +
                  str(report["google_aggregate"]["programs"]) + " programs")
        return 0
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError,
            subprocess.SubprocessError) as error:
        print("Example TDD gate rejected: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
