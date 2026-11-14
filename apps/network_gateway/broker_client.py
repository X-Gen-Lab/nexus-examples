#!/usr/bin/env python3
"""Adapt three broker-fixture bundles to five explicit gateway executions.

The pinned broker tool still owns credentials, Docker and withdrawal. This
adapter owns only its application child invocations and their ordinary process
receipts; it introduces no additional qualification report or fixture policy.
"""

import hashlib
import os
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("NEXUS_SOURCE", ROOT / "external/nexus")).resolve()
if not (SOURCE / "tools/dev/process.py").is_file():
    raise ValueError(
        f"Source SDK process helper missing: {SOURCE}; initialize external/nexus")
sys.path.insert(0, str(SOURCE))
from tools.dev import process


def execute_bundle(client, port, mode, credentials, output):
    """Preserve every real application exit; stop at the first failed child."""
    if mode not in {"clean", "fault", "missing-client"}:
        raise ValueError("Unknown broker fixture bundle")
    client = Path(client).resolve(strict=True)
    credentials = Path(credentials).resolve(strict=True)
    if not client.is_file() or not os.access(client, os.X_OK):
        raise ValueError("Gateway client must be an executable")
    if not credentials.is_dir() or not str(port).isdigit() or not 0 < int(port) <= 65535:
        raise ValueError("Expected a credential directory and a nonzero TCP port")
    output = Path(output).absolute()
    if output.is_relative_to(credentials):
        raise ValueError("Process evidence must not overwrite credential inputs")
    output.mkdir(parents=True, exist_ok=False)
    modes = ("clean", "backpressure", "deadline") if mode == "clean" else (mode,)
    print("gateway_binary_sha256=" + hashlib.sha256(client.read_bytes()).hexdigest(),
          flush=True)
    for scenario in modes:
        result = process.run(
            [str(client), str(port), scenario, str(credentials)],
            cwd=ROOT, env=os.environ.copy(), log=output / (scenario + ".log"),
            stage="private-gateway-" + scenario,
            timeout=5 if scenario == "deadline" else 7)
        print("gateway_application_case=" + scenario, flush=True)
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n",
              flush=True)
        if result.returncode:
            return result.returncode
    return 0


def main():
    if len(sys.argv) != 4:
        return 2
    try:
        client = Path(os.environ["NEXUS_GATEWAY_BINARY"])
        evidence = Path(os.environ["NEXUS_GATEWAY_INTEROP_OUTPUT"])
        return execute_bundle(
            client, sys.argv[1], sys.argv[2], Path(sys.argv[3]),
            evidence / (sys.argv[2] + "-" + uuid.uuid4().hex))
    except (KeyError, OSError, ValueError) as error:
        print("Gateway broker adapter rejected: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
