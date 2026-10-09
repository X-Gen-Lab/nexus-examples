#!/usr/bin/env python3
"""Forward an explicit example preset to CMake and, optionally, CTest."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time


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
    started = time.monotonic()
    with subprocess.Popen(arguments, cwd=ROOT, env=environment, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          bufsize=1) as process:
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        code = process.wait()
    result = f"Exit {code}; elapsed {time.monotonic() - started:.3f}s\n"
    print(result, end="", flush=True)
    log.write(result)
    log.flush()
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True,
                        help="configure/build preset from this repository")
    parser.add_argument("--nexus-source", type=Path,
                        default=ROOT / "external/nexus",
                        help="existing source SDK checkout; default: external/nexus")
    parser.add_argument("--parallel", type=positive_integer, default=4)
    parser.add_argument("--test", action="store_true",
                        help="also run the matching Native CTest preset")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.preset):
        parser.error("preset must be a name without path separators")
    source = args.nexus_source.resolve()
    if not (source / "CMakeLists.txt").is_file():
        parser.error(f"Nexus checkout is missing: {source}; initialize its submodule first")
    environment = os.environ.copy()
    environment["NEXUS_SOURCE"] = str(source)
    # Presets remain the only definition of the target and build directory.
    # This environment variable resolves ARM toolchainFile before project().
    output = ROOT / "build" / args.preset
    output.mkdir(parents=True, exist_ok=True)
    commands = [
        ["cmake", "--preset", args.preset, f"-DNEXUS_SOURCE:PATH={source}"],
        ["cmake", "--build", "--preset", args.preset,
         "--parallel", str(args.parallel)],
    ]
    if args.test:
        commands.append([
            "ctest", "--preset", args.preset, "--output-on-failure",
            "--no-tests=error", "--parallel", str(args.parallel),
            "--output-junit", str(output / "ctest-results.xml"),
        ])
    with (output / "build.log").open("w", encoding="utf-8") as log:
        try:
            for command in commands:
                code = run(command, environment, log)
                if code:
                    return code
        except OSError as error:
            message = f"Build command could not run: {error}\n"
            print(message, end="", file=sys.stderr)
            log.write(message)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
