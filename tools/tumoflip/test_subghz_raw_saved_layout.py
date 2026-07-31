#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
STANDARD_VIEW = REPO_ROOT / "applications/main/subghz/views/subghz_read_raw.c"
ARF_VIEW = REPO_ROOT / "applications_user/arf_subghz_full/views/subghz_read_raw.c"


class SubGhzRawSavedLayoutTest(unittest.TestCase):
    def read(self, path: Path) -> str:
        return path.read_text(encoding="utf-8")

    def draw_function(self, source: str) -> str:
        start = source.index("void subghz_read_raw_draw(Canvas*")
        end = source.index("bool subghz_read_raw_input(", start)
        return source[start:end]

    def test_standard_and_arf_raw_views_remain_in_sync(self) -> None:
        self.assertEqual(self.read(STANDARD_VIEW), self.read(ARF_VIEW))

    def test_saved_file_uses_dedicated_details_layout(self) -> None:
        draw = self.draw_function(self.read(STANDARD_VIEW))

        self.assertIn("const bool is_saved_file = !furi_string_empty(model->file_name)", draw)
        self.assertIn("subghz_read_raw_draw_saved_header(canvas, model)", draw)
        self.assertIn("subghz_read_raw_draw_saved_details(canvas, model)", draw)
        self.assertNotIn("elements_text_box(", draw)
        self.assertIn("} else if(!is_saved_file) {", draw)
        self.assertIn("subghz_read_raw_draw_rssi(canvas, model)", draw)

    def test_saved_details_show_meaningful_metadata(self) -> None:
        source = self.read(STANDARD_VIEW)

        self.assertIn('canvas_draw_str(canvas, 108, 8, "RAW")', source)
        self.assertIn('canvas_draw_str(canvas, 2, 26, "FREQ")', source)
        self.assertIn('canvas_draw_str(canvas, frequency_end + 2U, 26, "MHz")', source)
        self.assertIn('canvas_draw_str(canvas, 2, 44, "MOD")', source)
        self.assertIn('canvas_draw_str(canvas, 71, 44, "RADIO")', source)
        self.assertIn('? "INT" : "EXT"', source)

    def test_long_saved_names_are_bounded_and_ellipsized(self) -> None:
        source = self.read(STANDARD_VIEW)

        self.assertIn("SUBGHZ_RAW_SAVED_NAME_WIDTH       96U", source)
        self.assertIn("SUBGHZ_RAW_SAVED_PRESET_WIDTH     37U", source)
        self.assertIn("canvas_string_width(canvas, display_text)", source)
        self.assertIn('canvas_string_width(canvas, "...")', source)
        self.assertIn('strlcat(display_text, "...", display_text_size)', source)

    def test_transmission_keeps_animation_but_idle_saved_view_has_no_rssi_graph(self) -> None:
        draw = self.draw_function(self.read(STANDARD_VIEW))

        self.assertIn("if(is_transmitting) {", draw)
        self.assertIn("subghz_read_raw_draw_sin(canvas, model)", draw)
        self.assertLess(
            draw.index("if(is_transmitting) {"),
            draw.index("} else if(!is_saved_file) {"),
        )

    def test_saved_actions_use_refined_hierarchy_and_native_key_icons(self) -> None:
        source = self.read(STANDARD_VIEW)
        draw = self.draw_function(source)

        action_start = source.index("subghz_read_raw_draw_saved_center_action(")
        action_end = source.index("static bool subghz_read_raw_is_transmitting(", action_start)
        actions = source[action_start:action_end]

        self.assertIn("canvas_draw_rbox(canvas, button_x, 52, button_width, 12, 2)", actions)
        self.assertIn("canvas_draw_rframe(canvas, button_x, 52, button_width, 12, 2)", actions)
        self.assertIn("&I_ButtonCenter_7x7", actions)
        self.assertIn("&I_ButtonLeft_4x7", actions)
        self.assertIn("&I_ButtonRight_4x7", actions)
        self.assertNotIn("elements_button_left", actions)
        self.assertNotIn("elements_button_right", actions)
        self.assertIn('subghz_read_raw_draw_saved_center_action(canvas, "Send", true)', actions)
        self.assertIn(
            'subghz_read_raw_draw_saved_center_action(canvas, "Hold to repeat", false)',
            draw,
        )


if __name__ == "__main__":
    unittest.main()
