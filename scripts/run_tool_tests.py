#!/usr/bin/env python3
"""Run all owned tool contracts; missing or skipped execution rejects."""

from pathlib import Path
import sys
import unittest


def main():
    directory = Path(__file__).resolve().parents[1] / "tests/tooling"
    sys.path.insert(0, str(directory.parents[1]))
    suite = unittest.defaultTestLoader.discover(str(directory))
    if not suite.countTestCases():
        raise ValueError("No tool contracts were discovered")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() and not result.skipped else 1


if __name__ == "__main__":
    sys.exit(main())
