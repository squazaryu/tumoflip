#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class DesktopSlideshowFullscreenTest(unittest.TestCase):
    def test_slideshow_temporarily_hides_status_bar(self) -> None:
        source = (
            REPO_ROOT
            / "applications/services/desktop/scenes/desktop_scene_slideshow.c"
        ).read_text(encoding="utf-8")

        enter = source.split("void desktop_scene_slideshow_on_enter", 1)[1].split(
            "bool desktop_scene_slideshow_on_event", 1
        )[0]
        exit_scene = source.split("void desktop_scene_slideshow_on_exit", 1)[1]

        self.assertIn("gui_set_status_bar_hidden(desktop->gui, true)", enter)
        self.assertLess(
            enter.index("gui_set_status_bar_hidden"),
            enter.index("view_dispatcher_switch_to_view"),
        )
        self.assertIn("gui_set_status_bar_hidden(desktop->gui, false)", exit_scene)
        self.assertLess(
            exit_scene.index("gui_set_status_bar_hidden"),
            exit_scene.index("storage_common_remove"),
        )

    def test_gui_suppresses_only_status_bar_drawing(self) -> None:
        source = (REPO_ROOT / "applications/services/gui/gui.c").read_text(
            encoding="utf-8"
        )
        internal_header = (
            REPO_ROOT / "applications/services/gui/gui_i.h"
        ).read_text(encoding="utf-8")

        self.assertIn("bool status_bar_hidden;", internal_header)
        self.assertIn("gui_set_status_bar_hidden", internal_header)
        self.assertEqual(
            source.count("if(!gui->status_bar_hidden)"),
            2,
        )

    def test_slideshow_does_not_move_desktop_viewport(self) -> None:
        dispatcher = (
            REPO_ROOT / "applications/services/gui/view_dispatcher.c"
        ).read_text(encoding="utf-8")
        gui = (REPO_ROOT / "applications/services/gui/gui.c").read_text(
            encoding="utf-8"
        )
        scene = (
            REPO_ROOT
            / "applications/services/desktop/scenes/desktop_scene_slideshow.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("view_dispatcher_set_gui_type", dispatcher)
        self.assertNotIn("gui_view_port_set_layer", gui)
        self.assertNotIn("ViewDispatcherTypeFullscreen", scene)


if __name__ == "__main__":
    unittest.main()
