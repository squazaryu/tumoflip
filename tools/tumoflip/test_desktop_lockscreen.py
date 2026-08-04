#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopLockscreenTest(unittest.TestCase):
    def test_desktop_settings_migrate_lock_animation_toggle(self) -> None:
        header = (
            REPO_ROOT / "applications/services/desktop/desktop_settings.h"
        ).read_text(encoding="utf-8")
        source = (
            REPO_ROOT / "applications/services/desktop/desktop_settings.c"
        ).read_text(encoding="utf-8")

        self.assertIn("uint8_t lockscreen_skip_animation;", header)
        self.assertIn("uint8_t lockscreen_clock_enabled;", header)
        self.assertIn("#define DESKTOP_SETTINGS_VER_18 (18)", source)
        self.assertIn("#define DESKTOP_SETTINGS_VER_19 (19)", source)
        self.assertIn("#define DESKTOP_SETTINGS_VER    (20)", source)
        self.assertIn("DesktopSettingsV18", source)
        self.assertIn("DesktopSettingsV19", source)
        self.assertIn("desktop_settings_migrate_from_v18", source)
        self.assertIn("desktop_settings_migrate_from_v19", source)
        self.assertGreaterEqual(source.count("settings->lockscreen_skip_animation = 0;"), 3)
        self.assertGreaterEqual(source.count("settings->lockscreen_clock_enabled = 0;"), 4)

    def test_desktop_settings_exposes_lock_animation_toggle(self) -> None:
        source = (
            REPO_ROOT
            / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c"
        ).read_text(encoding="utf-8")

        self.assertIn("DesktopSettingsLockAnimation", source)
        self.assertIn('"Lock Animation"', source)
        self.assertIn("LOCK_ANIMATION_COUNT", source)
        self.assertIn("lockscreen_skip_animation_value[LOCK_ANIMATION_COUNT] = {0, 1}", source)
        self.assertIn("desktop_settings_scene_start_lock_animation_changed", source)
        self.assertIn("app->settings.lockscreen_skip_animation", source)

    def test_desktop_settings_exposes_lock_screen_mode(self) -> None:
        source = (
            REPO_ROOT
            / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c"
        ).read_text(encoding="utf-8")

        self.assertIn("DesktopSettingsLockScreen", source)
        self.assertIn('"Lock Screen"', source)
        self.assertIn('"Wallpaper"', source)
        self.assertIn('"Clock"', source)
        self.assertIn("LOCK_SCREEN_COUNT", source)
        self.assertIn("lock_screen_value[LOCK_SCREEN_COUNT] = {0, 1}", source)
        self.assertIn("desktop_settings_scene_start_lock_screen_changed", source)
        self.assertIn("app->settings.lockscreen_clock_enabled", source)

    def test_locked_view_can_skip_doors_without_touching_statusbar(self) -> None:
        locked = (
            REPO_ROOT / "applications/services/desktop/views/desktop_view_locked.c"
        ).read_text(encoding="utf-8")
        locked_h = (
            REPO_ROOT / "applications/services/desktop/views/desktop_view_locked.h"
        ).read_text(encoding="utf-8")
        desktop = (REPO_ROOT / "applications/services/desktop/desktop.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("bool skip_animation;", locked)
        self.assertIn("locked_view->skip_animation = false;", locked)
        self.assertIn("desktop_view_locked_set_skip_animation", locked)
        self.assertIn("locked_view->callback(DesktopLockedEventDoorsClosed", locked)
        self.assertIn("furi_timer_start(locked_view->timer, DOOR_MOVING_INTERVAL_MS)", locked)
        self.assertIn("desktop_view_locked_set_skip_animation", locked_h)
        self.assertIn(
            "desktop->settings.lockscreen_skip_animation",
            desktop,
        )

        self.assertNotIn("GuiLayerStatusBar", locked)
        self.assertNotIn("GuiLayerStatusBar", locked_h)

    def test_locked_view_clock_is_bounded_and_preserves_statusbar(self) -> None:
        locked = (
            REPO_ROOT / "applications/services/desktop/views/desktop_view_locked.c"
        ).read_text(encoding="utf-8")
        locked_h = (
            REPO_ROOT / "applications/services/desktop/views/desktop_view_locked.h"
        ).read_text(encoding="utf-8")
        events = (
            REPO_ROOT / "applications/services/desktop/views/desktop_events.h"
        ).read_text(encoding="utf-8")
        scene = (
            REPO_ROOT / "applications/services/desktop/scenes/desktop_scene_locked.c"
        ).read_text(encoding="utf-8")
        desktop = (REPO_ROOT / "applications/services/desktop/desktop.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("LOCKSCREEN_CLOCK_UPDATE_MS        (1000)", locked)
        self.assertIn("LOCKSCREEN_BATTERY_UPDATE_SECONDS (30)", locked)
        self.assertIn("FuriTimer* clock_timer;", locked)
        self.assertIn('"%02u:%02u:%02u"', locked)
        self.assertIn("furi_hal_rtc_get_datetime", locked)
        self.assertIn("furi_hal_power_get_pct", locked)
        self.assertIn("locale_get_time_format", locked)
        self.assertIn("locale_get_date_format", locked)
        self.assertIn("desktop_view_locked_clock_draw", locked)
        self.assertIn("STATUS_BAR_Y_SHIFT", locked)
        self.assertIn("desktop_view_locked_set_clock_enabled", locked_h)
        self.assertIn("desktop_view_locked_update_clock", locked_h)
        self.assertIn("DesktopLockedEventClockUpdate", events)
        self.assertIn("case DesktopLockedEventClockUpdate:", scene)
        self.assertIn("desktop->settings.lockscreen_clock_enabled", desktop)

        self.assertNotIn("GuiLayerStatusBar", locked)
        self.assertNotIn("GuiLayerStatusBar", locked_h)


if __name__ == "__main__":
    unittest.main()
