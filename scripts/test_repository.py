#!/usr/bin/env python3
"""Verify the pinned source SDK and its maintained dependency checkouts.

This checks source identity, not firmware execution. Use build.py --test for
Native execution and the SDK ELF validator for linked ARM firmware.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    "ext/freertos",
    "ext/googletest",
    "vendors/arm/CMSIS_5",
    "vendors/st/cmsis_device_f4",
    "vendors/st/stm32f4xx_hal_driver",
)


def checked_output(arguments, directory):
    result = subprocess.run(arguments, cwd=directory, text=True,
                            capture_output=True, timeout=60)
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or
                         f"Command failed with exit {result.returncode}")
    return result.stdout


def load_identity():
    identity = json.loads((ROOT / "dependencies/nexus.lock.json").read_text(encoding="utf-8"))
    required = {"repository", "commit", "tree", "consumption", "recursive_submodules"}
    if (not isinstance(identity, dict) or identity.get("schema_version") != 1 or
            not required.issubset(identity)):
        raise ValueError("Nexus source lock is missing its required identity fields")
    for field in ("commit", "tree"):
        if not isinstance(identity[field], str) or not re.fullmatch(r"[0-9a-f]{40}", identity[field]):
            raise ValueError(f"Nexus source lock requires a complete {field} SHA")
    if not isinstance(identity["repository"], str):
        raise ValueError("Nexus source lock requires a repository URL")
    url = urlparse(identity["repository"])
    if (url.scheme != "https" or not url.netloc or not url.path or
            url.username or url.password or url.query or url.fragment):
        raise ValueError("Nexus source lock requires an HTTPS repository URL without credentials")
    if (identity["consumption"] != "source-add_subdirectory" or
            identity["recursive_submodules"] is not True):
        raise ValueError("Nexus source lock must select the recursive source SDK contract")
    return identity


def verify_presets():
    document = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("CMake presets must be a JSON object")
    sections = {}
    for section in ("configurePresets", "buildPresets", "testPresets"):
        entries = document.get(section)
        if not isinstance(entries, list) or not entries:
            raise ValueError(f"CMake presets require a nonempty {section} list")
        indexed = {}
        for entry in entries:
            if (not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or
                    not entry["name"] or entry["name"] in indexed):
                raise ValueError(f"CMake presets contain an invalid or duplicate {section} name")
            indexed[entry["name"]] = entry
        sections[section] = indexed
    configured = sections["configurePresets"]
    visible = {name for name, entry in configured.items() if not entry.get("hidden", False)}
    if len(visible) != 9 or "native-debug" not in visible:
        raise ValueError("The reference matrix requires Native and eight ARM configure presets")
    for name, entry in configured.items():
        inherited = entry.get("inherits", [])
        if isinstance(inherited, str):
            inherited = [inherited]
        if not isinstance(inherited, list) or any(
                not isinstance(parent, str) or parent not in configured or parent == name
                for parent in inherited):
            raise ValueError(f"Configure preset {name} has an invalid inheritance reference")
    if set(sections["buildPresets"]) != visible:
        raise ValueError("Every reference configuration requires its matching build preset")
    for section in ("buildPresets", "testPresets"):
        for name, entry in sections[section].items():
            reference = entry.get("configurePreset")
            if not isinstance(reference, str) or reference not in visible:
                raise ValueError(f"{section} {name} references a missing or hidden configuration")
            if section == "buildPresets" and reference != name:
                raise ValueError(f"Build preset {name} must reference its matching configuration")
    native = sections["testPresets"].get("native-debug")
    if not native or native.get("configurePreset") != "native-debug":
        raise ValueError("Native execution requires its matching CTest preset")
    return sorted(visible)


def verify(source, override):
    identity = load_identity()
    presets = verify_presets()
    if not (source / "CMakeLists.txt").is_file():
        raise ValueError("Nexus checkout is missing; initialize external/nexus")
    revision = checked_output(["git", "rev-parse", "HEAD"], source).strip()
    if revision != identity["commit"]:
        raise ValueError(f"Nexus revision mismatch: expected {identity['commit']}, got {revision}")
    tree = checked_output(["git", "rev-parse", "HEAD^{tree}"], source).strip()
    if tree != identity["tree"]:
        raise ValueError(f"Nexus tree mismatch: expected {identity['tree']}, got {tree}")
    if checked_output(["git", "status", "--porcelain", "--untracked-files=normal"], source):
        raise ValueError("Nexus source or its dependency checkouts have uncommitted changes")
    entry = checked_output(["git", "ls-files", "--stage", "--", "external/nexus"], ROOT)
    expected = f"160000 {identity['commit']} 0\texternal/nexus\n"
    if entry != expected:
        raise ValueError("external/nexus must be a Git submodule pinned to the source lock")
    repository = checked_output([
        "git", "config", "--file", ".gitmodules", "--get", "submodule.external/nexus.url",
    ], ROOT).strip()
    if repository != identity["repository"]:
        raise ValueError("Nexus submodule repository does not match the source lock")
    status = checked_output([
        "git", "submodule", "status", "--recursive", "--", *DEPENDENCIES,
    ], source)
    modules = []
    for line in status.splitlines():
        if not line.startswith(" "):
            raise ValueError(f"Missing, conflicted or changed dependency checkout: {line}")
        fields = line[1:].split()
        if len(fields) < 2:
            raise ValueError("Invalid recursive submodule identity")
        modules.append({"path": fields[1], "revision": fields[0]})
    missing = set(DEPENDENCIES) - {entry["path"] for entry in modules}
    if missing:
        raise ValueError("Required pinned submodules are missing: " + ", ".join(sorted(missing)))
    vendor = checked_output([
        sys.executable, str(source / "scripts/ci/verify_vendor_source.py"),
        "--root", str(source / "vendors/gigadevice/gd32f4xx"),
    ], ROOT)
    return {
        "schema_version": 1,
        "kind": "example-source-dependency-identity",
        "nexus_repository": repository,
        "nexus_revision": revision,
        "nexus_tree": tree,
        "source_override": override,
        "example_gitlink_verified": True,
        "reference_presets": presets,
        "submodules": modules,
        "reviewed_vendor_import": json.loads(vendor),
        "hardware_verified": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nexus-source", type=Path,
                        help="local SDK checkout override; source lock and example Gitlink remain checked")
    parser.add_argument("--report", type=Path,
                        help="optional JSON source/dependency identity report")
    args = parser.parse_args(argv)
    try:
        if args.report:
            args.report.unlink(missing_ok=True)
        source = (args.nexus_source or ROOT / "external/nexus").resolve()
        report = verify(source, source != (ROOT / "external/nexus").resolve())
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Repository dependency verification failed: {error}\n")
    print(f"Verified Nexus {report['nexus_revision']}, "
          f"{len(report['reference_presets'])} presets, "
          f"{len(report['submodules'])} recursive dependency checkouts and the reviewed GD32 import")
    if report["source_override"]:
        print("Local SDK checkout location override; source lock, tree and example Gitlink verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
