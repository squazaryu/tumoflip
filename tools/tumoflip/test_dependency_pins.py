#!/usr/bin/env python3
"""Regression contracts for security-sensitive host and JavaScript dependencies."""

import json
from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
PACKAGES = REPO_ROOT / "applications/system/js_app/packages"


def versions(contents, package):
    return {tuple(map(int, version.split("."))) for version in re.findall(
        rf"^  {re.escape(package)}@(\d+\.\d+\.\d+)(?=[:(])", contents, re.M)}


class DependencyPinTests(unittest.TestCase):
    def test_documentation_resolves_patched_parser_versions(self):
        lock = (PACKAGES / "fz-sdk/pnpm-lock.yaml").read_text()
        for package, minimum in (("brace-expansion", (5, 0, 12)),
                                 ("markdown-it", (14, 3, 3)), ("minimatch", (11, 0, 1))):
            with self.subTest(package=package):
                resolved = versions(lock, package)
                self.assertTrue(resolved, f"no resolved {package} found")
                self.assertTrue(all(version >= minimum for version in resolved), resolved)

    def test_documentation_parsers_are_not_sdk_runtime_dependencies(self):
        package = json.loads((PACKAGES / "fz-sdk/package.json").read_text())
        for dependency in ("typedoc", "typedoc-material-theme"):
            self.assertNotIn(dependency, package["dependencies"])
            self.assertIn(dependency, package["devDependencies"])
        self.assertEqual(package["engines"]["node"], ">=18.12")

    def test_wizard_does_not_keep_the_generic_glob_chain(self):
        package = json.loads((PACKAGES / "create-fz-app/package.json").read_text())
        self.assertNotIn("replace-in-file", package["dependencies"])
        self.assertEqual(package["engines"]["node"], ">=18.12")
        lock = (PACKAGES / "create-fz-app/pnpm-lock.yaml").read_text()
        for dependency in ("replace-in-file", "glob", "minimatch", "brace-expansion", "yargs"):
            self.assertFalse(versions(lock, dependency), dependency)

    def test_audit_remains_strict_and_has_no_ignored_advisories(self):
        workflow = (REPO_ROOT / ".github/workflows/js-sdk.yml").read_text()
        self.assertGreaterEqual(workflow.count("pnpm audit --audit-level moderate"), 2)
        self.assertNotIn("--ignore-registry-errors", workflow)
        for directory in ("fz-sdk", "create-fz-app"):
            package = json.loads((PACKAGES / directory / "package.json").read_text())
            self.assertNotIn("auditConfig", package.get("pnpm", {}))


if __name__ == "__main__":
    unittest.main()
