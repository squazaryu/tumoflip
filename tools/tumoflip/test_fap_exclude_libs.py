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

    def test_f7_api_88_5_exports_shared_libgcc_experiment(self) -> None:
        self.assertIn("Version,+,88.5,,", API)
        for symbol in (
            "__adddf3",
            "__aeabi_d2f",
            "__aeabi_ddiv",
            "__fixdfsi",
            "__muldf3",
            "__udivmoddi4",
        ):
            self.assertIn(f"Function,+,{symbol},", API)

    def test_only_f7_nfc_and_js_manifests_opt_in(self) -> None:
        nfc = (REPO_ROOT / "applications/main/nfc/application.fam").read_text(
            encoding="utf-8"
        )
        js = (REPO_ROOT / "applications/system/js_app/application.fam").read_text(
            encoding="utf-8"
        )
        self.assertGreaterEqual(nfc.count('fap_exclude_libs=["gcc"]'), 60)
        self.assertGreaterEqual(js.count('fap_exclude_libs=["gcc"]'), 30)
        self.assertIn('appid="js_app_start"', js)
        self.assertNotIn(
            'appid="js_app_start",\n    fap_exclude_libs=["gcc"]', js
        )


if __name__ == "__main__":
    unittest.main()
