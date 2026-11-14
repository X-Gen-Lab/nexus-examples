#!/usr/bin/env python3
"""Execute every discovered product GoogleTest case and validate fresh evidence."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

from sdk_tools import load, sdk_root

OWNER = {"schema_version": 1, "kind": "nexus_example_google_execution"}


def identities(discovery):
    suite, cases = None, set()
    for line in discovery.splitlines():
        text = line.split("#", 1)[0].rstrip()
        if text and not text[0].isspace() and text.endswith("."):
            suite = text[:-1]
        elif suite is not None and text.startswith("  ") and text.strip():
            identity = (suite, text.strip())
            if identity in cases:
                raise ValueError("Repeated GoogleTest discovery identity")
            cases.add(identity)
    return cases


def run_google(source, binary, output, *, environment=None):
    paths = [Path(path).absolute() for path in (source, binary, output)]
    if any(part.is_symlink() for path in paths for part in (path, *path.parents)):
        raise ValueError("GoogleTest paths must not traverse symlinks")
    source, binary, output = (path.resolve() for path in paths)
    if (not binary.is_file() or binary.is_relative_to(output) or
            source.is_relative_to(output)):
        raise ValueError("GoogleTest output cannot replace inputs")
    for name in (".nexus-google-owner.json", "google-results.xml", "discovery.log",
                 "execution.log", "commands.json", "execution.json"):
        if (output / name).is_symlink():
            raise ValueError("GoogleTest output artifacts must not be symlinks")
    owner = output / ".nexus-google-owner.json"
    if output.exists() and (not owner.is_file() or
                            json.loads(owner.read_text()) != OWNER):
        raise ValueError("GoogleTest output directory is not owned")
    output.mkdir(parents=True, exist_ok=True)
    owner.write_text(json.dumps(OWNER) + "\n")
    (output / "execution.json").unlink(missing_ok=True)
    (output / "commands.json").unlink(missing_ok=True)
    validator = load(source, "scripts/validation/junit.py", "example_google_junit")
    env = {key: value for key, value in
           (environment if environment is not None else os.environ).items()
           if not key.startswith("GTEST_")}
    report = output / "google-results.xml"
    report.unlink(missing_ok=True)
    commands = []

    def execute(arguments, name):
        process = subprocess.run(arguments, env=env, text=True,
                                 capture_output=True, timeout=30)
        (output / (name + ".log")).write_text(process.stdout + process.stderr)
        commands.append({"argv": arguments, "exit_code": process.returncode})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if process.returncode:
            raise ValueError("GoogleTest command failed: " + name)
        return process.stdout

    discovery = execute([str(binary), "--gtest_list_tests"], "discovery")
    expected = identities(discovery)
    count = len(expected)
    if count == 0:
        raise ValueError("GoogleTest discovery contains no test cases")
    started = validator.prepare_report(report)
    execute([str(binary), "--gtest_output=xml:" + str(report)], "execution")
    try:
        result = validator.validate_junit(report, not_before_ns=started)
        cases = list(ET.parse(report).getroot().iter("testcase"))
        if (result["skipped"] or result["passed"] != count or any(
                case.get("status") != "run" or case.get("result") != "completed"
                for case in cases)):
            raise ValueError("GoogleTest report contains unexecuted cases")
        actual = {(case.get("classname"), case.get("name")) for case in cases}
    except (OSError, ET.ParseError) as error:
        raise ValueError("Missing or invalid fresh GoogleTest report") from error
    if actual != expected:
        raise ValueError("GoogleTest executed identities differ from discovery")
    result = {**result, "schema_version": 1,
              "kind": "example_google_execution", "discovered": count,
              "binary": str(binary), "commands": commands,
              "qualifies_source_sdk": False,
              "limits": "Execution only; source/SDK/HIL need separate identity"}
    (output / "execution.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nexus-source", type=Path)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = run_google(sdk_root(args.nexus_source), args.binary, args.output)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("Example GoogleTest execution rejected: " + str(error), file=sys.stderr)
        return 1
    print("Executed " + str(report["passed"]) + " product GoogleTest cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
