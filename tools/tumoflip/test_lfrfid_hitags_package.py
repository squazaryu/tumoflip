"""Contracts for the lazy Hitag S writer package."""

from pathlib import Path
import unittest

from tools.tumoflip.validate_release import (
    PACKAGE_ONLY_PACKAGE_FILES,
    PACKAGE_ONLY_PACKAGE_GROUPS,
    PACKAGE_RELEASE_OVERLAY_FILES,
    PACKAGE_RELEASE_OVERLAY_GROUPS,
    package_extapp_exports,
)


ROOT = Path(__file__).resolve().parents[2]
TARGET = "apps_data/lfrfid/plugins/lfrfid_hitags.fal"


class LfRfidHitagsPackageTests(unittest.TestCase):
    def test_hitags_plugin_is_a_base_package_only_export(self) -> None:
        manifest = (ROOT / "applications_user/lfrfid_hitags/application.fam").read_text()
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn("fal_embedded=False", manifest)
        self.assertIn(TARGET, PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(PACKAGE_ONLY_PACKAGE_GROUPS[TARGET], "base")
        self.assertIn(TARGET, PACKAGE_RELEASE_OVERLAY_FILES)
        self.assertEqual(PACKAGE_RELEASE_OVERLAY_GROUPS[TARGET], "base")
        self.assertEqual(package_extapp_exports()["lfrfid_hitags.fal"], TARGET)

    def test_package_workflows_build_the_lazy_plugin(self) -> None:
        for workflow in ("pr-build.yml", "release.yml"):
            contents = (ROOT / ".github/workflows" / workflow).read_text()
            self.assertIn("fap_lfrfid_hitags", contents)

    def test_package_only_plugins_are_not_copied_to_updater_resources(self) -> None:
        extapps = (ROOT / "scripts/fbt_tools/fbt_extapps.py").read_text()
        self.assertIn("deployable = self.app.is_default_deployable", extapps)

    def test_plugin_path_matches_lfrfid_assets_namespace(self) -> None:
        worker = (ROOT / "lib/lfrfid/lfrfid_worker_modes.c").read_text()
        self.assertIn('APP_ASSETS_PATH("plugins/lfrfid_hitags.fal")', worker)
        self.assertIn("LFRFID_HITAGS_PLUGIN_APP_ID", worker)


if __name__ == "__main__":
    unittest.main()
