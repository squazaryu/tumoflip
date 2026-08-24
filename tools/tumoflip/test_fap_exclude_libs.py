#!/usr/bin/env python3
"""Contracts for the opt-in shared-toolchain-library FAP experiment."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = (REPO_ROOT / "scripts/fbt/appmanifest.py").read_text(encoding="utf-8")
BUILDER = (REPO_ROOT / "scripts/fbt_tools/fbt_extapps.py").read_text(encoding="utf-8")
API = (REPO_ROOT / "targets/f7/api_symbols.csv").read_text(encoding="utf-8")


class FapExcludeLibsTest(unittest.TestCase):
    def test_manifest_field_is_explicitly_opt_in(self) -> None:
        self.assertIn("fap_exclude_libs: List[str]", MANIFEST)
        self.assertIn("fap_exclude_libs: List[str] = field(default_factory=list)", MANIFEST)

    def test_builder_removes_only_declared_libraries(self) -> None:
        self.assertIn("excluded = set(self.app.fap_exclude_libs)", BUILDER)
        self.assertIn(
            'LIBS=[lib for lib in self.app_env["LIBS"] if lib not in excluded]',
            BUILDER,
        )

    def test_api_88_0_remains_unchanged_until_exports_are_proven(self) -> None:
        self.assertIn("Version,+,88.0,,", API)
        self.assertNotIn("Version,+,88.4,,", API)


if __name__ == "__main__":
    unittest.main()
