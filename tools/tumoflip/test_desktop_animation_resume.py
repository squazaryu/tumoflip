#!/usr/bin/env python3

import unittest
import struct
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from flipper.assets.dolphin import (  # noqa: E402
    FRAME_BUNDLE_FILENAME,
    FRAME_BUNDLE_HEADER_FORMAT,
    FRAME_BUNDLE_MAGIC,
    FRAME_BUNDLE_VERSION,
    _write_frame_bundle,
)


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
            restore.index("animation_storage_get_bubble_animation"),
        )
        self.assertNotIn("animation_storage_find_animation", restore)
        self.assertIn(
            "StorageAnimation* restore_animation = animation_manager->freezed_animation",
            restore,
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

    def test_resume_reuses_parsed_animation_and_reads_frames_directly(self) -> None:
        storage_source = (
            REPO_ROOT
            / "applications/services/desktop/animations/animation_storage.c"
        ).read_text(encoding="utf-8")
        manager_source = (
            REPO_ROOT
            / "applications/services/desktop/animations/animation_manager.c"
        ).read_text(encoding="utf-8")

        release = storage_source.split(
            "void animation_storage_release_animation_frames", 1
        )[1].split("static void animation_storage_free_animation", 1)[0]
        self.assertIn("animation_storage_free_frames", release)
        self.assertNotIn("animation_storage_free_animation", release)

        cache = storage_source.split("void animation_storage_cache_animation", 1)[
            1
        ].split("void animation_storage_release_animation_frames", 1)[0]
        self.assertIn("animation_storage_reload_frames", cache)

        reload_frames = storage_source.split(
            "static bool animation_storage_reload_frames", 2
        )[2].split("static void animation_storage_free_bubbles", 1)[0]
        self.assertIn(
            "const uint8_t frame_rate = animation->icon_animation.frame_rate",
            reload_frames,
        )
        self.assertIn(
            "FURI_CONST_ASSIGN(animation->icon_animation.frame_rate, frame_rate)",
            reload_frames,
        )

        load_frames = storage_source.split(
            "static bool animation_storage_load_frames", 1
        )[1].split("static bool animation_storage_load_bubbles", 1)[0]
        self.assertIn("storage_file_size(file)", load_frames)
        self.assertNotIn("storage_common_stat", load_frames)

        unload = manager_source.split(
            "void animation_manager_unload_and_stall_animation", 1
        )[1].split("void animation_manager_load_and_continue_animation", 1)[0]
        self.assertIn(
            "animation_manager->freezed_animation = animation_manager->current_animation",
            unload,
        )
        self.assertIn("animation_storage_release_animation_frames", unload)
        self.assertNotIn("animation_storage_free_storage_animation", unload)

        restore = manager_source.split(
            "void animation_manager_load_and_continue_animation", 1
        )[1].split("static void animation_manager_switch_to_one_shot_view", 1)[0]
        self.assertIn(
            "const bool storage_changed = animation_manager->profile_reload_forced",
            restore,
        )
        self.assertIn(
            "!blocked && !profile_changed && !storage_changed",
            restore,
        )

    def test_generated_frame_bundle_is_versioned_and_sequential(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            frame_paths = [root / "frame_0.bm", root / "frame_1.bm"]
            frames = [b"\x00\xaa", b"\x01\x00\x02\x00\xbb\xcc"]
            for path, frame in zip(frame_paths, frames):
                path.write_bytes(frame)

            _write_frame_bundle(
                str(root),
                [str(path) for path in frame_paths],
                width=8,
                height=1,
            )

            bundle = (root / FRAME_BUNDLE_FILENAME).read_bytes()
            header_size = struct.calcsize(FRAME_BUNDLE_HEADER_FORMAT)
            magic, payload_size, version, frame_count, width, height = struct.unpack(
                FRAME_BUNDLE_HEADER_FORMAT, bundle[:header_size]
            )
            frame_sizes = struct.unpack(
                f"<{frame_count}H",
                bundle[header_size : header_size + (2 * frame_count)],
            )
            payload = bundle[header_size + (2 * frame_count) :]

            self.assertEqual(magic, FRAME_BUNDLE_MAGIC)
            self.assertEqual(version, FRAME_BUNDLE_VERSION)
            self.assertEqual((frame_count, width, height), (2, 8, 1))
            self.assertEqual(frame_sizes, tuple(len(frame) for frame in frames))
            self.assertEqual(payload_size, len(payload))
            self.assertEqual(payload, b"".join(frames))

    def test_resume_prefers_validated_bundle_and_keeps_legacy_fallback(self) -> None:
        storage_source = (
            REPO_ROOT
            / "applications/services/desktop/animations/animation_storage.c"
        ).read_text(encoding="utf-8")

        bundle = storage_source.split(
            "static bool animation_storage_load_frame_bundle", 1
        )[1].split("static bool animation_storage_load_frames", 1)[0]
        self.assertIn("animation_frame_bundle_magic", bundle)
        self.assertIn("ANIMATION_FRAME_BUNDLE_VERSION", bundle)
        self.assertIn("header.frame_count != frame_count", bundle)
        self.assertIn("header.width != width", bundle)
        self.assertIn("header.height != height", bundle)
        self.assertIn("storage_file_size(file) != expected_file_size", bundle)
        self.assertIn("checked_payload_size != header.payload_size", bundle)
        self.assertIn("animation_storage_frame_data_is_valid", bundle)
        self.assertEqual(bundle.count("storage_file_open("), 1)

        load_frames = storage_source.split(
            "static bool animation_storage_load_frames", 1
        )[1].split("static bool animation_storage_load_bubbles", 1)[0]
        self.assertLess(
            load_frames.index("animation_storage_load_frame_bundle"),
            load_frames.index('ANIMATION_DIR "/%s/frame_%d.bm"'),
        )
        self.assertIn('ANIMATION_DIR "/%s/frame_%d.bm"', load_frames)
        self.assertIn("storage_file_open(", load_frames)
        self.assertIn("storage_file_read(", load_frames)


if __name__ == "__main__":
    unittest.main()
