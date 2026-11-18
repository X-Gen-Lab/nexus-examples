#!/usr/bin/env python3
"""Forward an explicit example preset to CMake and, optionally, CTest."""

import argparse
import os
from pathlib import Path
import re
import shlex
import sys
import time

from sdk_tools import load, maintenance


ROOT = Path(__file__).resolve().parents[1]


def positive_integer(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("parallel jobs must be greater than zero")
    return number


def run(arguments, environment, log):
    command = "+ " + shlex.join(arguments) + "\n"
    print(command, end="", flush=True)
    log.write(command)
    log.flush()
    _, process = maintenance(environment.get("NEXUS_SOURCE"))
    log_name = getattr(log, "name", None)
    attempt = (Path(log_name).parent / ("command-" + str(time.time_ns()) + ".log")
               if isinstance(log_name, str) else None)
    result = process.run(arguments, cwd=ROOT, env=environment, log=attempt,
                         stage="example-build")
    output = result.stdout + result.stderr
    print(output, end="", flush=True)
    log.write(output)
    log.write(f"Exit {result.returncode}\n")
    log.flush()
    return result.returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True,
                        help="configure/build preset from this repository")
    parser.add_argument("--nexus-source", type=Path,
                        help="existing source SDK checkout; default: external/nexus")
    parser.add_argument("--parallel", type=positive_integer, default=4)
    parser.add_argument("--test", action="store_true",
                        help="also run the matching Native CTest preset")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.preset):
        parser.error("preset must be a name without path separators")
    source = Path(args.nexus_source or os.environ.get("NEXUS_SOURCE") or
                  ROOT / "external/nexus").resolve()
    if not (source / "CMakeLists.txt").is_file():
        parser.error(f"Nexus checkout is missing: {source}; initialize its submodule first")
    environment = os.environ.copy()
    environment["NEXUS_SOURCE"] = str(source)
    # Resolve the same include/inheritance graph as the selected SDK. The
    # configure preset owns the directory; build/test presets must agree.
    try:
        presets = load(source, "tools/dev/presets.py", "example_sdk_presets")
        presets = presets.Presets(ROOT, environment)
        selected = presets.selected("build", args.preset)
        configure_name = selected["configurePreset"]
        configure = presets.selected("configure", configure_name)
        if args.test:
            test = presets.selected("test", args.preset)
            if test["configurePreset"] != configure_name:
                raise ValueError("Build and test presets require the same "
                                 "configure preset")
        output = Path(configure["binaryDir"])
    except (OSError, ValueError) as error:
        print("Build rejected: " + str(error), file=sys.stderr)
        return 1
    commands = [
        ["cmake", "--preset", configure_name, f"-DNEXUS_SOURCE:PATH={source}"],
        ["cmake", "--build", "--preset", args.preset,
         "--parallel", str(args.parallel)],
    ]
    if args.test:
        commands.append([
            "ctest", "--preset", args.preset, "--output-on-failure",
            "--no-tests=error", "--parallel", str(args.parallel),
            "--output-junit", str(output / "ctest-results.xml"),
        ])
        commands.append([
            sys.executable, str(source / "scripts/validation/junit.py"),
            "--report", str(output / "ctest-results.xml"),
        ])
    lock, _ = maintenance(source)
    try:
        with lock.build_lock(output):
            with (output / "build.log").open("w", encoding="utf-8") as log:
                try:
                    for command in commands:
                        if command[0] == "ctest":
                            (output / "ctest-results.xml").unlink(missing_ok=True)
                            test_started_ns = time.time_ns()
                        elif "--report" in command:
                            command.extend(["--not-before-ns", str(test_started_ns)])
                        code = run(command, environment, log)
                        if code:
                            return code
                except OSError as error:
                    message = f"Build command could not run: {error}\n"
                    print(message, end="", file=sys.stderr)
                    log.write(message)
                    return 1
    except (OSError, ValueError) as error:
        print("Build rejected: " + str(error), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
