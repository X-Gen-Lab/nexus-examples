#!/usr/bin/env python3
"""Load public maintenance helpers from the explicitly selected source SDK."""

import importlib.util
import os
from pathlib import Path
import sys


# The SDK is source input, including imports made after these helpers return.
# Keep bytecode disabled for the lifetime of this consumer process.
sys.dont_write_bytecode = True


ROOT = Path(__file__).resolve().parents[1]


def sdk_root(override=None):
    source = Path(override or os.environ.get("NEXUS_SOURCE", ROOT / "external/nexus")).resolve()
    if not (source / "CMakeLists.txt").is_file():
        raise ValueError(f"Source SDK missing: {source}; initialize external/nexus")
    return source


def load(source, relative, name):
    path = source / relative
    if not path.is_file():
        raise ValueError(f"SDK maintenance API missing: {path}; update the pinned SDK")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def maintenance(source=None):
    """Reuse the selected SDK's process and kernel build-lease contracts."""
    source = sdk_root(source)
    if str(source) not in sys.path:
        sys.path.insert(0, str(source))
    from tools.dev import build_lock, process
    return build_lock, process
