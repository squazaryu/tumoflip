#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DolphinProgressionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.state_h = (
            REPO_ROOT / "applications/services/dolphin/helpers/dolphin_state.h"
        ).read_text(encoding="utf-8")
        self.state_c = (
            REPO_ROOT / "applications/services/dolphin/helpers/dolphin_state.c"
        ).read_text(encoding="utf-8")
        self.dolphin_c = (REPO_ROOT / "applications/services/dolphin/dolphin.c").read_text(
            encoding="utf-8"
        )
        self.passport_c = (
            REPO_ROOT / "applications/settings/dolphin_passport/passport.c"
        ).read_text(encoding="utf-8")
        self.animation_manager_c = (
            REPO_ROOT / "applications/services/desktop/animations/animation_manager.c"
        ).read_text(encoding="utf-8")

    def test_progression_uses_100_level_curve(self) -> None:
        self.assertIn("#define DOLPHIN_LEVEL_MAX 100U", self.state_h)
        self.assertIn("#define LEVEL_BASE_STEP_XP           100U", self.state_c)
        self.assertIn("#define LEVEL_STEP_GROWTH_XP         10U", self.state_c)
        self.assertNotIn("LEVEL2_THRESHOLD", self.state_c)
        self.assertNotIn("LEVEL3_THRESHOLD", self.state_c)

        thresholds = [
            level * 100 + ((level * (level - 1) // 2) * 10)
            for level in range(1, 100)
        ]
        self.assertEqual(thresholds[0], 100)
        self.assertEqual(thresholds[1], 210)
        self.assertEqual(thresholds[-1], 58410)
        self.assertTrue(
            all(
                thresholds[index] - thresholds[index - 1]
                < thresholds[index + 1] - thresholds[index]
                for index in range(1, len(thresholds) - 1)
            )
        )

    def test_levelup_pending_checks_exact_threshold(self) -> None:
        self.assertIn(
            "dolphin_state_is_levelup(dolphin->state->data.icounter)",
            self.dolphin_c,
        )
        self.assertNotIn(
            "!dolphin_state_xp_to_levelup(dolphin->state->data.icounter)",
            self.dolphin_c,
        )

    def test_passport_accepts_levels_above_three(self) -> None:
        self.assertIn("DOLPHIN_LEVEL_MAX", self.passport_c)
        self.assertIn("PORTRAIT_LEVELS_TOTAL", self.passport_c)
        self.assertIn("portrait_level = MIN", self.passport_c)
        self.assertNotIn("stats->level == 3", self.passport_c)
        self.assertNotIn("stats->level <= 3", self.passport_c)

    def test_idle_animations_are_not_level_gated(self) -> None:
        valid_animation_block = self.animation_manager_c.split(
            "animation_manager_is_valid_idle_animation", 1
        )[1].split("return result;", 1)[0]

        self.assertIn("Tumoflip unlocks default idle animations", valid_animation_block)
        self.assertNotIn("stats->level < info->min_level", valid_animation_block)
        self.assertNotIn("stats->level > info->max_level", valid_animation_block)

    def test_levelup_animation_handles_high_levels(self) -> None:
        self.assertIn(
            "(stats.level > 1) && (stats.level < DOLPHIN_LEVEL_MAX)",
            self.animation_manager_c,
        )
        self.assertNotIn("stats.level == 2", self.animation_manager_c)


if __name__ == "__main__":
    unittest.main()
