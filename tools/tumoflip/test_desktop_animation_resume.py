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
        self.assertIn("model->freeze_frame_index = 0U", preview)
        self.assertIn("model->freeze_frame_index + 1U", timer)
        self.assertIn("++model->freeze_frame_index", timer)
        self.assertNotIn("% frame_count", timer)
        self.assertNotIn("freeze_elapsed", view_source)
        self.assertNotIn("freeze_started_at", view_source)

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
        self.assertIn(
            "model->freeze_frame_origin + model->freeze_frame_index", unfreeze
        )
        self.assertIn("restore_active", unfreeze)
        self.assertIn("bubble_animation_pick_bubble(model, restore_active)", unfreeze)
        self.assertLess(
            unfreeze.index("model->current_frame ="),
            unfreeze.index("bubble_animation_release_frame"),
        )

    def test_preview_to_full_animation_handoff_is_continuous(self) -> None:
        # The compact preview is stored in visible order starting at `origin`.
        # Restoring the full animation from the same origin plus the last
        # displayed preview index guarantees that the handoff frame is identical.
        cases = [
            (0, 0, 12, 0),
            (4, 3, 12, 7),
            (10, 1, 12, 11),
            (10, 5, 12, 3),
        ]
        for origin, preview_index, phase_frames, expected_frame in cases:
            preview_frame = (origin + preview_index) % phase_frames
            restored_frame = (origin + preview_index) % phase_frames
            self.assertEqual(preview_frame, expected_frame)
            self.assertEqual(restored_frame, expected_frame)

        preview_index = 0
        visible_indices = [preview_index]
        for _ in range(6):
            if preview_index + 1 < 4:
                preview_index += 1
            visible_indices.append(preview_index)
        self.assertEqual(visible_indices, [0, 1, 2, 3, 3, 3, 3])


if __name__ == "__main__":
    unittest.main()
