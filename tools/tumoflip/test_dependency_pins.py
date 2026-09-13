#!/usr/bin/env python3
"""Regression contracts for security-sensitive host and JavaScript dependencies."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BRACE_EXPANSION_INTEGRITY = (
    "sha512-ScQ4IuvIEF1TMlP7Zt+vjJ//9zlPb2SDcxWxM3bk8s6t6GGdJ7KO1dCcTidOPJKePW30LE/2cT7wCyPho9/Wxg=="
)
LOCKFILES = (
    REPO_ROOT / "applications/system/js_app/packages/create-fz-app/pnpm-lock.yaml",
    REPO_ROOT / "applications/system/js_app/packages/fz-sdk/pnpm-lock.yaml",
)


class DependencyPinTests(unittest.TestCase):
    def test_brace_expansion_is_patched_in_both_package_locks(self) -> None:
        for lockfile in LOCKFILES:
            with self.subTest(lockfile=lockfile):
                contents = lockfile.read_text(encoding="utf-8")
                self.assertIn("brace-expansion@5.0.9:", contents)
                self.assertIn(BRACE_EXPANSION_INTEGRITY, contents)
                self.assertNotIn("brace-expansion@5.0.8", contents)


if __name__ == "__main__":
    unittest.main()
