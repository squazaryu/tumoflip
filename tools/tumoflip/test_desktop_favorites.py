#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopFavoritesTest(unittest.TestCase):
    def test_favorite_settings_offer_folder_and_fap_targets(self) -> None:
        source = (
            REPO_ROOT
            / "applications/settings/desktop_settings/scenes/desktop_settings_scene_favorite.c"
        ).read_text(encoding="utf-8")

        self.assertIn('#define MODULE_ONE_FOLDER_PATH  EXT_PATH("apps/Module One")', source)
        self.assertIn('#define ARF_TOOLS_FOLDER_PATH  EXT_PATH("apps/ARF Tools")', source)
        self.assertIn('.extension = ".fap"', source)
        self.assertIn('EXTERNAL_APPLICATION_NAME  ("[Select App]")', source)
        self.assertIn('#define JS_RUNNER_APP_NAME    "JS Runner"', source)
        self.assertIn("favorite_fap_is_shortcut_visible(name)", source)
        self.assertNotIn('.extension = ".fap|.js"', source)

    def test_desktop_launches_favorite_targets_by_type(self) -> None:
        source = (
            REPO_ROOT / "applications/services/desktop/scenes/desktop_scene_main.c"
        ).read_text(encoding="utf-8")

        self.assertIn("desktop_scene_main_is_apps_folder_target(target)", source)
        self.assertIn(
            "loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, target)",
            source,
        )
        self.assertNotIn('#define JS_RUNNER_APP "JS Runner"', source)

    def test_profile_file_close_does_not_dispatch_from_storage_callback(self) -> None:
        source = (
            REPO_ROOT
            / "applications/services/desktop/animations/animation_manager.c"
        ).read_text(encoding="utf-8")

        file_close_case = source.split("case StorageEventTypeFileClose:", 1)[1].split(
            "default:", 1
        )[0]
        self.assertIn("animation_manager->profile_reload_pending = true", file_close_case)
        self.assertIn("furi_timer_start(", file_close_case)
        self.assertNotIn("check_blocking_callback", file_close_case)

        timer_callback = source.split(
            "static void animation_manager_profile_reload_timer_callback", 1
        )[1].split("static void", 1)[0]
        self.assertIn("check_blocking_callback", timer_callback)


if __name__ == "__main__":
    unittest.main()
