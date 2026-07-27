#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CLOCK_SOURCE = REPO_ROOT / "applications/main/clock_app/clock_app.c"
CLOCK_HEADER = REPO_ROOT / "applications/main/clock_app/clock_app.h"
CLOCK_MANIFEST = REPO_ROOT / "applications/main/clock_app/application.fam"
RELEASE_VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class ClockNightstandTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = CLOCK_SOURCE.read_text(encoding="utf-8")
        cls.header = CLOCK_HEADER.read_text(encoding="utf-8")
        cls.manifest = CLOCK_MANIFEST.read_text(encoding="utf-8")
        cls.release_validator = RELEASE_VALIDATOR.read_text(encoding="utf-8")

    def test_alarm_uses_native_rtc_service(self) -> None:
        self.assertIn("furi_hal_rtc_get_alarm(&app->alarm_time)", self.source)
        self.assertIn(
            "furi_hal_rtc_set_alarm(&app->alarm_time, app->alarm_enabled)",
            self.source,
        )
        self.assertNotIn("alarm_melody", self.source)
        self.assertNotIn("ns_check_alarm", self.source)
        self.assertNotIn("alarm_firing", self.header)

    def test_only_private_brightness_is_persisted(self) -> None:
        settings = self.header.split("typedef struct {", 1)[1].split(
            "} NsSettings;", 1
        )[0]
        self.assertIn("uint8_t brightness", settings)
        self.assertNotIn("alarm_enabled", settings)
        self.assertIn("#define NS_SETTINGS_VERSION        3", self.header)
        self.assertIn("#define NS_SETTINGS_LEGACY_VERSION 2", self.header)
        self.assertIn("NsSettingsLegacyV2 legacy", self.source)
        self.assertIn("ns_settings_save(app)", self.source)

    def test_alarm_editor_back_cancels_and_ok_saves(self) -> None:
        editor = self.source.split("static bool alarm_time_input", 1)[1].split(
            "// ---------------- periodic tick", 1
        )[0]
        ok_case = editor.split("case InputKeyOk:", 1)[1].split(
            "case InputKeyBack:", 1
        )[0]
        back_case = editor.split("case InputKeyBack:", 1)[1].split("default:", 1)[0]

        self.assertIn("clock_alarm_apply(app)", ok_case)
        self.assertIn("ns_switch(app, NsViewAlarmMenu)", ok_case)
        self.assertNotIn("clock_alarm_apply(app)", back_case)
        self.assertIn("return false", back_case)

    def test_display_state_is_restored_on_all_exit_paths(self) -> None:
        restore = self.source.split("static void clock_restore_notification", 1)[
            1
        ].split("static void clock_free_gui", 1)[0]
        self.assertIn("saved_display_off_delay_ms", restore)
        self.assertIn("sequence_display_backlight_enforce_auto", restore)
        self.assertIn("saved_brightness", restore)
        self.assertIn("led_reset", restore)

        entry = self.source.split("int32_t clock_app", 1)[1]
        self.assertGreaterEqual(entry.count("clock_restore_notification(app)"), 2)
        self.assertIn("furi_timer_start(app->timer", entry)
        self.assertIn("!= FuriStatusOk", entry)

    def test_ui_and_package_metadata_describe_the_real_behavior(self) -> None:
        self.assertIn('elements_button_right(canvas, "Alarm")', self.source)
        self.assertIn('"L/R field  U/D value"', self.source)
        self.assertIn('elements_button_center(canvas, "Save")', self.source)
        self.assertIn(
            "canvas_draw_line(canvas, ux - uw / 2, 42, ux + uw / 2, 42)",
            self.source,
        )
        self.assertIn(
            'canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignBottom, '
            '"L/R field  U/D value")',
            self.source,
        )
        self.assertIn('fap_version="1.3.1"', self.manifest)
        self.assertIn("native alarm", self.manifest)
        self.assertIn(
            'resources / "apps/Tools/clock.fap"', self.release_validator
        )


if __name__ == "__main__":
    unittest.main()
