#!/usr/bin/env python3
"""Replay the formal example CI using the existing presets and SDK checkers.

Dependencies must be prepared explicitly. This command neither installs host
packages nor operates equipment. A failed command stops the run and retains its
actual output; no report from an earlier run can substitute for execution.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from build import positive_integer
from sdk_tools import ROOT, load
import tdd_gate


def prepare_output(output):
    """Retain earlier runs and refuse redirected output paths."""
    if any(path.is_symlink() for path in (output, *output.parents)):
        raise ValueError("Local CI output must not traverse a symlink")
    if output.exists():
        raise ValueError("Local CI output already exists; select a new run directory")
    output.mkdir(parents=True)


def require_tools(names, environment):
    """Fail before compilation when a required host command is absent."""
    result = {}
    for name in names:
        executable = shutil.which(name, path=environment.get("PATH", ""))
        if executable is None:
            raise ValueError("Required local CI tool is missing: " + name)
        result[name] = executable
    return result


def arm_presets(root, selected=()):
    """Select the existing visible CMake matrix without another build graph."""
    document = json.loads((root / "CMakePresets.json").read_text())
    names = [entry["name"] for entry in document["configurePresets"]
             if not entry.get("hidden") and entry["name"] != "native-debug"]
    if any(name not in names for name in selected) or len(set(selected)) != len(selected):
        raise ValueError("ARM preset must be a unique maintained CMake preset")
    return list(selected) if selected else names


def standalones(preset):
    """Existing independent CMake consumers selected by their workflow rows."""
    recipes = {
        "stm32-qiming-freertos": ("apps/shared_spi_runtime", "shared_spi_runtime"),
        "stm32-qiming-baremetal": ("templates/qiming", "nexus_starter"),
        "stm32-sky-baremetal": ("templates/sky", "nexus_starter"),
        "stm32-qiming-core-baremetal": ("templates/qiming-core", "nexus_starter"),
        "stm32-qiming-v41-baremetal": ("templates/qiming-v41", "nexus_starter"),
    }
    return [(*recipes[preset], None)] if preset in recipes else []


def fixture_for(preset):
    """Only reviewed fixture identities may produce unbound station plans."""
    prefixes = {
        "stm32-qiming": "stm32f407_qiming_v31",
        "stm32-sky": "stm32f407ve_sky_qingchun",
        "stm32-qiming-core": "stm32f407zg_qiming_core",
        "stm32-qiming-v41": "stm32f407zg_qiming_v41",
    }
    board, _, backend = preset.rpartition("-")
    if board not in prefixes or backend not in ("baremetal", "freertos"):
        raise ValueError("No reviewed HIL fixture selection for preset: " + preset)
    return prefixes[board]


def validate_junit(source, report, started):
    validator = load(source, "scripts/validation/junit.py", "example_local_ci_junit")
    result = validator.validate_junit(report, not_before_ns=started)
    if result["skipped"]:
        raise ValueError("Local CI cannot qualify skipped installed consumer tests")
    return result


def verify_sdk(prefix, identity):
    """Check expected identity; find_package also verifies every payload hash."""
    path = prefix / "share/nexus/src/.nexus-source-sdk.json"
    document = json.loads(path.read_text())
    if (not isinstance(document, dict) or document.get("publishable") is not True or
            document.get("source_dirty") is not False or
            document.get("source_revision") != identity["nexus_revision"] or
            document.get("source_tree") != identity["nexus_tree"]):
        raise ValueError("Installed SDK must be publishable and match the exact formal source pin")
    return document


def verify_arm_cache(build, toolchain):
    """A warm CMake cache must not retain an unrelated compiler installation."""
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        return
    for line in cache.read_text().splitlines():
        key, _, value = line.partition("=")
        compiler = key.partition(":")[0]
        if compiler in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_ASM_COMPILER"):
            name = "arm-none-eabi-g++" if compiler == "CMAKE_CXX_COMPILER" else "arm-none-eabi-gcc"
            if Path(value).resolve() != (toolchain / "bin" / name).resolve():
                raise ValueError("CMake compiler differs from verified ARM toolchain; "
                                 "remove this stale build directory: " + str(build))


class LocalCI:
    """Thin orchestration; existing tools own source, build and report checks."""

    def __init__(self, root, source, output, jobs):
        self.root, self.source, self.output, self.jobs = root, source, output, jobs
        self.environment = tdd_gate.child_environment(root, os.environ)
        self.environment.pop("NEXUS_SOURCE", None)
        self.environment["NEXUS_BUILD_JOBS"] = str(jobs)
        self.environment["CMAKE_BUILD_PARALLEL_LEVEL"] = str(jobs)
        self.environment["PYTHONDONTWRITEBYTECODE"] = "1"
        self.identity = None
        self.report = {"schema_version": 1, "kind": "example_local_ci",
                       "status": "running", "commands": [], "parallel": jobs,
                       "physical_status": "not_executed"}

    def command(self, name, arguments, environment=None):
        """Reuse the ordinary gate's command/log format, including failures."""
        log = self.output / (name + ".log")
        self.report["commands"].append(str(log.with_suffix(".command.json")))
        return tdd_gate.execute(self.root, arguments,
                                environment or self.environment, log)

    def source_identity(self, name):
        path = self.output / (name + ".json")
        self.command(name, [sys.executable, "scripts/test_repository.py",
                           "--nexus-source", self.source, "--report", path])
        return json.loads(path.read_text())

    def native(self, installed_sdk):
        if installed_sdk is not None:
            verify_sdk(installed_sdk, self.identity)
            self.command("sdk-verify", [sys.executable,
                         self.source / "cmake/package/package_source_sdk.py",
                         "--verify", installed_sdk / "share/nexus/src"])
        self.command("style", [sys.executable, "scripts/check_style.py", "--all",
                               "--nexus-source", self.source, "--report",
                               self.output / "style.json"])
        environment = dict(self.environment, NEXUS_SOURCE=str(self.source))
        self.command("native", [sys.executable, "scripts/tdd_gate.py", "--all"], environment)
        # The gate owns complete catalogue, fresh JUnit and all Google reports.
        self.report["native_gate"] = str(self.root / "build/native-debug/tdd/execution.json")
        prefix = installed_sdk or self.output / "source-sdk"
        if installed_sdk is None:
            self.command("sdk-package", [sys.executable,
                         self.source / "cmake/package/package_source_sdk.py",
                         "--source", self.source, "--output", prefix, "--host-support",
                         "--component", "network", "--component", "tls",
                         "--component", "mqtt"])
        self.installed(prefix)

    def installed(self, prefix):
        metadata = verify_sdk(prefix, self.identity)
        self.report["installed_sdk"] = {"prefix": str(prefix), **{
            key: metadata.get(key) for key in
            ("source_revision", "source_tree", "snapshot_sha256", "publishable")}}
        build = self.output / "installed-native"
        build.mkdir()
        self.command("installed-configure", [
            "cmake", "-S", self.root / "reproduction", "-B", build, "-G", "Ninja",
            f"-DNexus_DIR={prefix / 'lib/cmake/Nexus'}",
            "-DNEXUS_EXPECTED_SOURCE_REVISION=" + self.identity["nexus_revision"],
            f"-DNEXUS_ASSEMBLY_FILE={self.root / 'reproduction/assemblies/native.toml'}",
            "-DNEXUS_BUILD_TESTS=OFF", "-DNEXUS_BUILD_WORKLOAD=OFF",
            "-DNEXUS_TEST_SUPPORT=ON",
            "-DNEXUS_ENABLE_SANITIZERS=ON", "-DCMAKE_BUILD_TYPE=Debug"])
        self.command("installed-build", ["cmake", "--build", build,
                                          "--parallel", str(self.jobs)])
        report = build / "ctest-results.xml"
        validator = load(self.source, "scripts/validation/junit.py", "example_local_ci_anchor")
        started = validator.prepare_report(report)
        self.command("installed-ctest", ["ctest", "--test-dir", build,
                     "--output-on-failure", "--no-tests=error", "--parallel", str(self.jobs),
                     "--output-junit", report])
        self.report["installed_ctest"] = validate_junit(self.source, report, started)
        self.command("installed-sdk-after", [sys.executable,
                     self.source / "cmake/package/package_source_sdk.py",
                     "--verify", prefix / "share/nexus/src"])
        if verify_sdk(prefix, self.identity) != metadata:
            raise ValueError("Installed SDK identity changed during consumer execution")

    def budget(self, name, build, target):
        self.command(name, [sys.executable, self.source / "tools/measurement/resources.py",
                     "--elf", build / "bin" / (target + ".elf"),
                     "--resolved", build / "nexus/generated/resolved.json",
                     "--budget", build / "nexus/generated/resource-budget.json",
                     "--report", self.output / (name + ".json")])

    def arm(self, presets, toolchain, cache):
        prepare = [sys.executable, self.source / "scripts/ci/install_arm_toolchain.py",
                   "--install-dir", toolchain]
        if cache:
            prepare.extend(["--cache-dir", cache])
        self.command("arm-toolchain", prepare)
        self.environment["PATH"] = str(toolchain / "bin") + os.pathsep + self.environment["PATH"]
        for preset in presets:
            build = self.root / "build" / preset
            verify_arm_cache(build, toolchain)
            self.command(preset, [sys.executable, "scripts/build.py", "--preset", preset,
                         "--nexus-source", self.source, "--parallel", str(self.jobs)])
            verify_arm_cache(build, toolchain)
            for target in ("platform_reference", "uart_owner_runtime"):
                self.budget(preset + "-" + target, build, target)
            for relative, target, assembly in standalones(preset):
                name = preset + "-" + target
                standalone = self.output / name
                configure = ["cmake", "-S", self.root / relative, "-B", standalone,
                             "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                             f"-DCMAKE_TOOLCHAIN_FILE={self.source / 'cmake/toolchains/arm-gcc.cmake'}",
                             f"-DNEXUS_SOURCE={self.source}"]
                if assembly:
                    configure.append(f"-DNEXUS_ASSEMBLY_FILE={self.source / assembly}")
                self.command(name + "-configure", configure)
                self.command(name + "-build", ["cmake", "--build", standalone,
                             "--target", target, "--parallel", str(self.jobs)])
                self.budget(name + "-resources", standalone, target)
            fixture = fixture_for(preset)
            if fixture:
                self.command(preset + "-hil", [sys.executable, "-B",
                             self.source / "tools/hil/prepare.py", "--fixture",
                             self.source / "tools/hil/fixtures" / (fixture + ".json"),
                             "--resolved", build / "nexus/generated/resolved.json",
                             "--report", self.output / (preset + "-hil.json")])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nexus-source", type=Path, default=ROOT / "external/nexus")
    parser.add_argument("--output", type=Path, required=True, help="new run directory; never overwritten")
    parser.add_argument("--scope", choices=("all", "native", "arm"), default="all")
    parser.add_argument("--parallel", type=positive_integer, default=1)
    parser.add_argument("--preset", action="append", default=[], help="ARM subset; report remains partial")
    parser.add_argument("--installed-sdk", type=Path, help="reuse a complete publishable SDK with the same pin")
    parser.add_argument("--arm-toolchain", type=Path, help="locked installer destination for ARM checks")
    parser.add_argument("--toolchain-cache", type=Path, help="original ARM archive cache; reverified by installer")
    args = parser.parse_args(argv)
    if args.scope != "native" and args.arm_toolchain is None:
        parser.error("--arm-toolchain is required for ARM checks")
    if args.preset and args.scope == "native":
        parser.error("--preset selects ARM checks only")
    output = args.output.expanduser().absolute()
    source = args.nexus_source.expanduser().resolve()
    runner = None
    try:
        prepare_output(output)
        runner = LocalCI(ROOT, source, output, args.parallel)
        runner.report.update(scope=args.scope, started_ns=time.time_ns(),
                             complete_matrix=args.scope == "all" and not args.preset,
                             python={"executable": sys.executable, "version": sys.version})
        runner.report["tools"] = require_tools(("git", "cmake", "ctest", "ninja", "gcc", "g++"),
                                               runner.environment)
        tdd_gate.check_snapshot(ROOT)
        initial = tdd_gate.staged_tree(ROOT)
        runner.report["index_tree"] = initial
        runner.identity = runner.source_identity("source")
        runner.report["source"] = runner.identity
        runner.command("doctor", [sys.executable, source / "tools/dev/dev.py",
                                   "doctor", "--target", "tools"])
        for name, executable in runner.report["tools"].items():
            runner.command("version-" + name, [executable, "--version"])
        if args.scope != "arm":
            runner.native(args.installed_sdk.resolve() if args.installed_sdk else None)
        if args.scope != "native":
            presets = arm_presets(ROOT, args.preset)
            runner.report["arm_presets"] = presets
            runner.arm(presets, args.arm_toolchain.resolve(), args.toolchain_cache)
        final = runner.source_identity("source-after")
        tdd_gate.check_snapshot(ROOT)
        if final != runner.identity or initial != tdd_gate.staged_tree(ROOT):
            raise ValueError("Example snapshot or source identity changed during local CI")
        runner.report["status"] = "passed"
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if runner:
            runner.report.update(status="failed", failure=str(error))
        print("Local CI failed: " + str(error), file=sys.stderr)
        return 1
    finally:
        if runner:
            runner.report["finished_ns"] = time.time_ns()
            (output / "execution.json").write_text(json.dumps(runner.report, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
