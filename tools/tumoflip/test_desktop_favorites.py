#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopFavoritesTest(unittest.TestCase):
    def test_favorite_settings_offer_folder_and_script_targets(self) -> None:
        source = (
            REPO_ROOT
            / "applications/settings/desktop_settings/scenes/desktop_settings_scene_favorite.c"
        ).read_text(encoding="utf-8")

        self.assertIn('#define MODULE_ONE_FOLDER_PATH  EXT_PATH("apps/Module One")', source)
        self.assertIn('#define ARF_TOOLS_FOLDER_PATH  EXT_PATH("apps/ARF Tools")', source)
        self.assertIn('.extension = ".fap|.js"', source)
        self.assertIn('furi_string_end_with(file_path, ".js")', source)
        self.assertIn("I_js_script_10px", source)

    def test_desktop_launches_favorite_targets_by_type(self) -> None:
        source = (
            REPO_ROOT / "applications/services/desktop/scenes/desktop_scene_main.c"
        ).read_text(encoding="utf-8")

        self.assertIn('#define JS_RUNNER_APP "JS Runner"', source)
        self.assertIn('desktop_scene_main_path_ends_with(target, ".js")', source)
        self.assertIn(
            "loader_start_detached_with_gui_error(desktop->loader, JS_RUNNER_APP, target)",
            source,
        )
        self.assertIn("desktop_scene_main_is_apps_folder_target(target)", source)
        self.assertIn(
            "loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, target)",
            source,
        )


if __name__ == "__main__":
    unittest.main()
