"""Weather Editor is a package-owned export, never an updater resource."""
from pathlib import Path
import unittest

from tools.tumoflip.validate_release import (
    PACKAGE_ONLY_PACKAGE_FILES,
    PACKAGE_RELEASE_OVERLAY_FILES,
    PACKAGE_RELEASE_OVERLAY_GROUPS,
    package_extapp_exports,
)

ROOT = Path(__file__).resolve().parents[2]
TARGET = "apps/Sub-GHz/weather_editor.fap"


class WeatherPackageExportTests(unittest.TestCase):
    def test_weather_editor_has_one_canonical_package_export(self):
        self.assertEqual(package_extapp_exports().get("weather_editor.fap"), TARGET)
        self.assertIn(TARGET, PACKAGE_ONLY_PACKAGE_FILES)
        self.assertIn(TARGET, PACKAGE_RELEASE_OVERLAY_FILES)
        self.assertEqual(PACKAGE_RELEASE_OVERLAY_GROUPS.get(TARGET), "base")

    def test_manifest_is_explicitly_package_only(self):
        manifest = (ROOT / "applications_user/weather_editor/application.fam").read_text()
        self.assertIn('fap_dist_path="apps/Sub-GHz/weather_editor.fap"', manifest)
        self.assertIn("fap_package_only=True", manifest)


if __name__ == "__main__":
    unittest.main()
