#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopSlideshowFullscreenTest(unittest.TestCase):
    def test_slideshow_temporarily_uses_fullscreen_layer(self) -> None:
        source = (
            REPO_ROOT
            / "applications/services/desktop/scenes/desktop_scene_slideshow.c"
        ).read_text(encoding="utf-8")

        enter = source.split("void desktop_scene_slideshow_on_enter", 1)[1].split(
            "bool desktop_scene_slideshow_on_event", 1
        )[0]
        exit_scene = source.split("void desktop_scene_slideshow_on_exit", 1)[1]

        self.assertIn("ViewDispatcherTypeFullscreen", enter)
        self.assertLess(
            enter.index("view_dispatcher_set_gui_type"),
            enter.index("view_dispatcher_switch_to_view"),
        )
        self.assertIn("ViewDispatcherTypeDesktop", exit_scene)
        self.assertLess(
            exit_scene.index("view_dispatcher_set_gui_type"),
            exit_scene.index("storage_common_remove"),
        )

    def test_dispatcher_moves_its_existing_viewport(self) -> None:
        source = (
            REPO_ROOT / "applications/services/gui/view_dispatcher.c"
        ).read_text(encoding="utf-8")
        internal_header = (
            REPO_ROOT / "applications/services/gui/view_dispatcher_i.h"
        ).read_text(encoding="utf-8")

        setter = source.split("void view_dispatcher_set_gui_type", 1)[1].split(
            "void view_dispatcher_draw_callback", 1
        )[0]
        self.assertIn("view_dispatcher_set_gui_type", internal_header)
        self.assertIn("gui_view_port_set_layer", setter)
        self.assertNotIn("gui_remove_view_port", setter)
        self.assertNotIn("gui_add_view_port", setter)

    def test_layer_move_preserves_in_flight_input_route(self) -> None:
        source = (REPO_ROOT / "applications/services/gui/gui.c").read_text(
            encoding="utf-8"
        )
        internal_header = (
            REPO_ROOT / "applications/services/gui/gui_i.h"
        ).read_text(encoding="utf-8")

        setter = source.split("void gui_view_port_set_layer", 1)[1].split(
            "void gui_view_port_send_to_front", 1
        )[0]
        self.assertIn("gui_view_port_set_layer", internal_header)
        self.assertIn("ViewPortArray_remove", setter)
        self.assertIn("ViewPortArray_push_back", setter)
        self.assertNotIn("view_port_gui_set", setter)
        self.assertNotIn("ongoing_input_view_port", setter)


if __name__ == "__main__":
    unittest.main()
