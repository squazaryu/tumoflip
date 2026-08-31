#!/usr/bin/env python3
"""Source contracts for the dedicated Standard Sub-GHz Auto Decode screen."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
RECEIVER_VIEW = REPO_ROOT / "applications/main/subghz/views/receiver.c"
RECEIVER_HEADER = REPO_ROOT / "applications/main/subghz/views/receiver.h"
SUBGHZ_TYPES = REPO_ROOT / "applications/main/subghz/helpers/subghz_types.h"
DECODE_SCENE = REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_decode_raw.c"
SAVE_SUCCESS_SCENE = (
    REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_save_success.c"
)


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def region(contents: str, start: str, end: str) -> str:
    start_index = contents.index(start)
    end_index = contents.index(end, start_index)
    return contents[start_index:end_index]


class SubGhzAutoDecodeLayoutTest(unittest.TestCase):
    def test_auto_decode_has_a_dedicated_receiver_mode_and_states(self) -> None:
        types = source(SUBGHZ_TYPES)
        header = source(RECEIVER_HEADER)

        self.assertIn("SubGhzViewReceiverModeAutoDecode", types)
        for state in (
            "Scanning",
            "Found",
            "NoMatch",
            "PackError",
            "ReadError",
            "RestoreError",
        ):
            self.assertIn(f"SubGhzViewReceiverAutoDecodeState{state}", header)
        self.assertIn("subghz_view_receiver_set_auto_decode(", header)

    def test_auto_decode_draw_returns_before_generic_receiver_chrome(self) -> None:
        view = source(RECEIVER_VIEW)
        draw = region(
            view,
            "void subghz_view_receiver_draw(",
            "static void subghz_view_receiver_timer_callback(",
        )

        auto_branch = """if(model->mode == SubGhzViewReceiverModeAutoDecode) {
        subghz_view_receiver_draw_auto_decode(canvas, model);
        return;
    }"""
        self.assertIn(auto_branch, draw)
        self.assertLess(draw.index(auto_branch), draw.index("SubGhzViewReceiverModeLive"))
        self.assertLess(draw.index(auto_branch), draw.index("model->frequency_str"))

    def test_scanning_layout_owns_bounded_rows_without_overlap(self) -> None:
        view = source(RECEIVER_VIEW)
        auto_draw = region(
            view,
            "static void subghz_view_receiver_draw_auto_decode(",
            "void subghz_view_receiver_draw(",
        )
        compact_draw = " ".join(auto_draw.split())

        # Header, content, progress, pack steps, and controls have distinct bands.
        self.assertIn("canvas_draw_str( canvas, 16, 10,", compact_draw)
        self.assertIn("canvas_draw_line(canvas, 2, 13, 125, 13);", auto_draw)
        self.assertIn('canvas_draw_str(canvas, 4, 25, "PACK");', auto_draw)
        self.assertIn("elements_string_fit_width(canvas, fitted, 88);", auto_draw)
        self.assertIn("canvas, 4, 30, 120,", auto_draw)
        self.assertIn('elements_button_left(canvas, "Cancel");', auto_draw)

        pack_steps = region(
            view,
            "static void subghz_view_receiver_draw_auto_decode_pack_steps(",
            "static void subghz_view_receiver_draw_auto_decode(",
        )
        self.assertIn("canvas_draw_rbox(canvas, x, 45, segment_width, 5, 1);", pack_steps)
        self.assertIn("canvas_draw_rframe(canvas, x, 45, segment_width, 5, 1);", pack_steps)
        self.assertIn("if(count == 0 || count > 8) return;", pack_steps)

    def test_found_and_terminal_states_use_short_fitted_content(self) -> None:
        view = source(RECEIVER_VIEW)
        auto_draw = region(
            view,
            "static void subghz_view_receiver_draw_auto_decode(",
            "void subghz_view_receiver_draw(",
        )

        self.assertIn('canvas_draw_str(canvas, 4, 38, "PROTOCOL");', auto_draw)
        self.assertIn("elements_string_fit_width(canvas, fitted, 120);", auto_draw)
        self.assertIn('elements_button_center(canvas, "Details");', auto_draw)
        for title, detail in (
            ("No match", "All packs checked"),
            ("Pack issue", "Some packs skipped"),
            ("RAW read error", "Try another capture"),
            ("Restore failed", "Restart Sub-GHz"),
        ):
            self.assertIn(f'title = "{title}";', auto_draw)
            self.assertIn(f'detail = "{detail}";', auto_draw)

    def test_auto_input_accepts_only_cancel_and_found_details(self) -> None:
        view = source(RECEIVER_VIEW)
        input_handler = region(
            view,
            "bool subghz_view_receiver_input(",
            "void subghz_view_receiver_enter(",
        )
        auto_input = region(input_handler, "if(auto_decode) {", "bool consumed = false;")

        self.assertIn("InputKeyBack", auto_input)
        self.assertIn("SubGhzCustomEventViewReceiverBack", auto_input)
        self.assertIn("auto_decode_has_result", auto_input)
        self.assertIn("InputKeyOk", auto_input)
        self.assertIn("SubGhzCustomEventViewReceiverOK", auto_input)
        self.assertNotIn("InputKeyUp", auto_input)
        self.assertNotIn("InputKeyDown", auto_input)
        self.assertGreater(auto_input.rfind("return true;"), auto_input.index("InputKeyOk"))

    def test_auto_decode_clears_stale_receiver_lock(self) -> None:
        view = source(RECEIVER_VIEW)
        scene = source(DECODE_SCENE)
        set_lock = region(
            view,
            "void subghz_view_receiver_set_lock(",
            "void subghz_view_receiver_set_callback(",
        )

        self.assertIn("subghz_receiver->lock = lock;", set_lock)
        self.assertIn("furi_timer_stop(subghz_receiver->timer);", set_lock)
        self.assertIn(
            "subghz_view_receiver_set_lock(subghz->subghz_receiver, false);", scene
        )

    def test_restore_failure_exits_instead_of_leaking_temporary_pack(self) -> None:
        scene = source(DECODE_SCENE)
        save_success = source(SAVE_SUCCESS_SCENE)
        cleanup = region(
            scene,
            "bool subghz_scene_decode_raw_cleanup(",
            "static void subghz_scene_decode_raw_set_result(",
        )

        self.assertIn("const bool restored =", cleanup)
        self.assertIn("if(restored) {", cleanup)
        self.assertIn("return restored;", cleanup)
        self.assertIn(
            'FURI_LOG_E(TAG, "Protocol pack restore failed; closing Sub-GHz");',
            scene,
        )
        for contents in (scene, save_success):
            self.assertIn("if(!subghz_scene_decode_raw_cleanup(subghz))", contents)
            self.assertIn("scene_manager_stop(subghz->scene_manager);", contents)
            self.assertIn("view_dispatcher_stop(subghz->view_dispatcher);", contents)

    def test_scene_keeps_auto_and_decode_current_visually_separate(self) -> None:
        scene = source(DECODE_SCENE)

        self.assertIn("SubGhzViewReceiverModeAutoDecode", scene)
        self.assertIn("SubGhzViewReceiverModeFile", scene)
        self.assertIn("subghz_view_receiver_set_auto_decode(", scene)
        self.assertNotIn('"%s %u/%u %s"', scene)
        self.assertIn("strtoul(furi_string_get_cstr(progress_str), NULL, 10)", scene)
        self.assertIn("(uint8_t)MIN(progress, (uint32_t)100)", scene)
        self.assertIn("if(!subghz->decode_raw_auto) subghz_scene_receiver_update_statusbar", scene)

    def test_file_progress_does_not_share_its_row_with_live_status(self) -> None:
        view = source(RECEIVER_VIEW)
        draw = region(
            view,
            "void subghz_view_receiver_draw(",
            "static void subghz_view_receiver_timer_callback(",
        )

        self.assertIn("if(model->mode != SubGhzViewReceiverModeLive) return;", draw)
        self.assertIn("canvas_draw_str(canvas, 3, 62, furi_string_get_cstr(model->progress_str));", draw)


if __name__ == "__main__":
    unittest.main()
