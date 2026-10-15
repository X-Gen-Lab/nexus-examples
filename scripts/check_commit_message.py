#!/usr/bin/env python3
"""Forward commit message validation to the pinned source SDK policy."""

import sys

from sdk_tools import load, sdk_root


if __name__ == "__main__":
    try:
        policy = load(sdk_root(), "scripts/ci/check_commit_message.py", "example_sdk_commit")
        raise SystemExit(policy.main())
    except (OSError, ValueError) as error:
        print(f"Commit policy unavailable: {error}", file=sys.stderr)
        raise SystemExit(1)
