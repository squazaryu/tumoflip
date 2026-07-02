#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class BleKillerIntegrationTest(unittest.TestCase):
    def test_ble_killer_source_import_is_removed(self) -> None:
        self.assertFalse((REPO_ROOT / "applications_user/ble_killer").exists())

    def test_release_and_deploy_do_not_include_ble_killer(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")
        readme = (REPO_ROOT / "ReadMe.md").read_text(encoding="utf-8")

        self.assertNotIn('"ble_killer"', validate)
        self.assertNotIn("ble_killer.fap", deploy)
        self.assertNotIn("BLE Killer", docs)
        self.assertNotIn("ble_killer.fap", readme)

    def test_retired_ble_killer_visible_app_is_cleaned_from_sd(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            '"/ext/apps/ARF Tools/ble_killer.fap": ARF_VISIBLE_PATHS["arf_subghz_full"]',
            validate,
        )


if __name__ == "__main__":
    unittest.main()
