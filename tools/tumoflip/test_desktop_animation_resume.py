#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopAnimationResumeTest(unittest.TestCase):
    def test_freeze_keeps_a_bounded_preview_from_the_visible_frame(self) -> None:
        source = (
            REPO_ROOT
            / "applications/services/desktop/animations/views/bubble_animation_view.c"
        ).read_text(encoding="utf-8")

        clone = source.split("bubble_animation_clone_preview(", 1)[1].split(
            "static void bubble_animation_release_frame", 1
        )[0]
        self.assertIn("BUBBLE_ANIMATION_RESUME_PREVIEW_MAX_BYTES", clone)
        self.assertIn("preview_offset", clone)
        self.assertIn("(origin + i) % preview_frames", clone)
        self.assertIn("animation->frame_order[order_index]", clone)
        self.assertIn("memcpy(frame, source_frame, frame_size)", clone)
        self.assertNotIn("compress_icon_decode", clone)

        freeze = source.split("void bubble_animation_freeze", 1)[1].split(
            "void bubble_animation_start_resume_preview", 1
        )[0]
        self.assertIn("model->freeze_frame_origin", freeze)
        self.assertIn("model->current_frame - phase_offset", freeze)
        self.assertIn("model->freeze_active", freeze)
        self.assertIn("model->freeze_frame_origin)", freeze)
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
        timer = view_source.split("static void bubble_animation_timer_callback", 1)[
            1
        ].split("static size_t bubble_animation_frame_data_size", 1)[0]
        self.assertIn("frame_count > 1U", preview)
        self.assertIn("furi_timer_start", preview)
        self.assertIn("bubble_animation_freeze_elapsed_frames(model)", timer)
        self.assertIn("bubble_animation_freeze_elapsed_frames(model)", preview)

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

        unfreeze = view_source.split("void bubble_animation_unfreeze", 1)[1]
        self.assertIn("model->freeze_frame_origin + elapsed_frames", unfreeze)
        self.assertIn("bubble_animation_freeze_elapsed_frames(model)", unfreeze)
        self.assertIn("restore_active", unfreeze)
        self.assertIn("bubble_animation_pick_bubble(model, restore_active)", unfreeze)
        self.assertLess(
            unfreeze.index("model->current_frame ="),
            unfreeze.index("bubble_animation_release_frame"),
        )


if __name__ == "__main__":
    unittest.main()
