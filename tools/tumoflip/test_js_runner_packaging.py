#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class JsRunnerPackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = (
            REPO_ROOT / "applications/system/js_app/application.fam"
        ).read_text(encoding="utf-8")
        self.desktop = (
            REPO_ROOT / "applications/services/desktop/scenes/desktop_scene_main.c"
        ).read_text(encoding="utf-8")
        self.archive = (
            REPO_ROOT / "applications/main/archive/scenes/archive_scene_browser.c"
        ).read_text(encoding="utf-8")

    def test_js_runner_is_external_menu_app(self) -> None:
        js_app_block = self.manifest.split('appid="js_app"', 1)[1].split("App(", 1)[0]

        self.assertIn("apptype=FlipperAppType.MENUEXTERNAL", js_app_block)
        self.assertIn('name="JS Runner"', js_app_block)
        self.assertIn('fap_category="Scripts"', js_app_block)
        self.assertIn('fap_icon="icon.png"', js_app_block)
        self.assertNotIn("provides=[\"js_app_start\"]", js_app_block)

    def test_archive_still_targets_js_runner_by_name(self) -> None:
        self.assertIn('"JS Runner"', self.archive)

    def test_desktop_favorites_do_not_target_js_runner(self) -> None:
        self.assertNotIn('"JS Runner"', self.desktop)


if __name__ == "__main__":
    unittest.main()
