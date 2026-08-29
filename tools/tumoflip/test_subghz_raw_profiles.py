#!/usr/bin/env python3
"""Regression contracts for independent Standard and Read RAW radio profiles."""

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PROFILE_ROOTS = (
    REPO_ROOT / "applications/main/subghz",
    REPO_ROOT / "applications_user/arf_subghz_full",
)


def function_body(source: str, name: str, next_name: str) -> str:
    start = source.index(name)
    end = source.index(next_name, start)
    return source[start:end]


class SubGhzRawProfilesTest(unittest.TestCase):
    def test_profiles_are_stored_and_old_settings_migrate_once(self) -> None:
        for root in PROFILE_ROOTS:
            with self.subTest(root=root.relative_to(REPO_ROOT)):
                header = (root / "subghz_last_settings.h").read_text(encoding="utf-8")
                source = (root / "subghz_last_settings.c").read_text(encoding="utf-8")

                self.assertIn("uint32_t raw_frequency;", header)
                self.assertIn("uint32_t raw_preset_index;", header)
                self.assertIn('"RawFrequency"', source)
                self.assertIn('"RawPreset"', source)
                self.assertIn("instance->raw_frequency = 0;", source)
                self.assertIn("instance->raw_preset_index = UINT32_MAX;", source)
                self.assertIn("instance->raw_frequency = instance->frequency;", source)
                self.assertIn("instance->raw_preset_index = instance->preset_index;", source)
                self.assertIn(
                    "furi_hal_subghz_is_frequency_valid(instance->raw_frequency)", source
                )
                self.assertGreaterEqual(source.count("instance->raw_frequency"), 5)
                self.assertGreaterEqual(source.count("instance->raw_preset_index"), 5)

    def test_read_raw_starts_from_the_raw_profile(self) -> None:
        for root in PROFILE_ROOTS:
            with self.subTest(root=root.relative_to(REPO_ROOT)):
                source = (root / "scenes/subghz_scene_read_raw.c").read_text(
                    encoding="utf-8"
                )
                enter = function_body(
                    source,
                    "void subghz_scene_read_raw_on_enter",
                    "bool subghz_scene_read_raw_on_event",
                )

                self.assertRegex(
                    enter,
                    re.compile(
                        r"subghz_txrx_set_preset_internal\(\s*"
                        r"subghz->txrx,\s*"
                        r"subghz->last_settings->raw_frequency,\s*"
                        r"subghz->last_settings->raw_preset_index,",
                        re.S,
                    ),
                )

    def test_raw_config_updates_only_the_raw_profile(self) -> None:
        for root in PROFILE_ROOTS:
            with self.subTest(root=root.relative_to(REPO_ROOT)):
                source = (root / "scenes/subghz_scene_receiver_config.c").read_text(
                    encoding="utf-8"
                )
                frequency = function_body(
                    source,
                    "static void subghz_scene_receiver_config_set_frequency",
                    "static void subghz_scene_receiver_config_set_preset",
                )
                preset = function_body(
                    source,
                    "static void subghz_scene_receiver_config_set_preset",
                    "static void subghz_scene_receiver_config_set_hopping",
                )

                self.assertIn("subghz_scene_receiver_config_is_raw", source)
                self.assertIn("if(is_raw", frequency)
                self.assertIn("last_settings->raw_frequency", frequency)
                self.assertIn("last_settings->frequency", frequency)
                self.assertIn("if(is_raw", preset)
                self.assertIn("last_settings->raw_preset_index", preset)
                self.assertIn("last_settings->preset_index", preset)

    def test_tumospectrum_capture_does_not_mutate_standard_profile(self) -> None:
        source = (REPO_ROOT / "applications/main/subghz/subghz.c").read_text(
            encoding="utf-8"
        )
        capture = source[
            source.index("if(open_capture_at_frequency") : source.index(
                "if(alloc_for_tx)", source.index("if(open_capture_at_frequency")
            )
        ]

        self.assertIn("last_settings->raw_frequency = capture_frequency;", capture)
        self.assertNotIn("last_settings->frequency =", capture)
        self.assertNotIn("last_settings->hopping_mode =", capture)


if __name__ == "__main__":
    unittest.main()
