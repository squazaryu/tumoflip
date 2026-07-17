#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopAnimationResumeTest(unittest.TestCase):
    def test_freeze_keeps_a_two_frame_preview(self) -> None:
        source = (
            REPO_ROOT
            / "applications/services/desktop/animations/views/bubble_animation_view.c"
        ).read_text(encoding="utf-8")

        clone = source.split("static Icon* bubble_animation_clone_preview", 1)[1].split(
            "static void bubble_animation_release_frame", 1
        )[0]
        self.assertIn("uint8_t frame_indices[2]", clone)
        self.assertIn("animation->passive_frames ?", clone)
        self.assertIn("animation->active_frames", clone)
        self.assertIn("compress_icon_decode", clone)
        self.assertIn("bitmap_size + 1U", clone)

        freeze = source.split("void bubble_animation_freeze", 1)[1].split(
            "void bubble_animation_start_resume_preview", 1
        )[0]
        self.assertIn("bubble_animation_clone_preview(model->current)", freeze)
        self.assertIn("furi_timer_stop(view->timer)", freeze)

    def test_preview_runs_only_during_desktop_resume(self) -> None:
        view_source = (
            REPO_ROOT
            / "applications/services/desktop/animations/views/bubble_animation_view.c"
        ).read_text(encoding="utf-8")
        manager_source = (
            REPO_ROOT
            / "applications/services/desktop/animations/animation_manager.c"
        ).read_text(encoding="utf-8")

        preview = view_source.split("void bubble_animation_start_resume_preview", 1)[1].split(
            "void bubble_animation_unfreeze", 1
        )[0]
        self.assertIn("frame_count > 1U", preview)
        self.assertIn("furi_timer_start", preview)

        restore = manager_source.split(
            "void animation_manager_load_and_continue_animation", 1
        )[1].split("static void animation_manager_switch_to_one_shot_view", 1)[0]
        self.assertLess(
            restore.index("bubble_animation_start_resume_preview"),
            restore.index("animation_manager_reload_profile"),
        )
        self.assertLess(
            restore.index("bubble_animation_start_resume_preview"),
            restore.index("animation_storage_find_animation"),
        )
        self.assertLess(
            restore.index("animation_storage_find_animation"),
            restore.index("bubble_animation_unfreeze"),
        )


if __name__ == "__main__":
    unittest.main()
