#!/usr/bin/env python3
"""Contracts for Quac ownership after the last bundled Dev 008-015 release."""

from pathlib import Path
import unittest

try:
    from . import validate_release
except ImportError:
    import validate_release


ARF_LEGACY_PATHS = validate_release.ARF_LEGACY_PATHS
BASE_LEGACY_PATHS = validate_release.BASE_LEGACY_PATHS
MODULE_ONE_LEGACY_PATHS = validate_release.MODULE_ONE_LEGACY_PATHS
PACKAGE_ONLY_PACKAGE_FILES = validate_release.PACKAGE_ONLY_PACKAGE_FILES
PACKAGE_ONLY_PACKAGE_GROUPS = validate_release.PACKAGE_ONLY_PACKAGE_GROUPS
PACKAGE_RELEASE_OVERLAY_FILES = validate_release.PACKAGE_RELEASE_OVERLAY_FILES
PACKAGE_RELEASE_OVERLAY_GROUPS = validate_release.PACKAGE_RELEASE_OVERLAY_GROUPS
package_extapp_exports = validate_release.package_extapp_exports


ROOT = Path(__file__).resolve().parents[2]
QUAC_SOURCE = "apps/Tools/quac.fap"
QUAC_TARGET = "/ext/apps/Tools/quac.fap"
QUAC_DATA = "/ext/apps_data/quac"


class QuacPackageMigrationTest(unittest.TestCase):
    def test_quac_is_built_but_not_bundled_in_updater_resources(self) -> None:
        manifest = (ROOT / "applications_user/quac/application.fam").read_text()
        self.assertIn('appid="quac"', manifest)
        self.assertIn('fap_category="Tools"', manifest)
        self.assertIn('fap_version="0.9.3"', manifest)
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn(QUAC_SOURCE, PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(PACKAGE_ONLY_PACKAGE_GROUPS[QUAC_SOURCE], "base")
        self.assertEqual(package_extapp_exports()["quac.fap"], QUAC_SOURCE)

    def test_quac_is_an_explicit_independent_overlay(self) -> None:
        self.assertIn(QUAC_SOURCE, PACKAGE_RELEASE_OVERLAY_FILES)
        self.assertEqual(PACKAGE_RELEASE_OVERLAY_GROUPS[QUAC_SOURCE], "base")
        self.assertEqual(QUAC_TARGET, f"/ext/{QUAC_SOURCE}")

    def test_quac_user_data_is_never_package_owned_or_cleaned(self) -> None:
        owned = {*PACKAGE_ONLY_PACKAGE_FILES, *PACKAGE_RELEASE_OVERLAY_FILES}
        self.assertFalse(any(path.startswith("apps_data/quac") for path in owned))
        cleanup = {
            *BASE_LEGACY_PATHS,
            *BASE_LEGACY_PATHS.values(),
            *ARF_LEGACY_PATHS,
            *ARF_LEGACY_PATHS.values(),
            *MODULE_ONE_LEGACY_PATHS,
            *MODULE_ONE_LEGACY_PATHS.values(),
        }
        self.assertFalse(any(path.startswith(QUAC_DATA) for path in cleanup))

    def test_migration_contract_runs_in_pr_and_release_ci(self) -> None:
        for workflow in ("pr-build.yml", "release.yml"):
            contents = (ROOT / ".github/workflows" / workflow).read_text()
            self.assertIn("tools/tumoflip/test_quac_package_migration.py", contents)


if __name__ == "__main__":
    unittest.main()
