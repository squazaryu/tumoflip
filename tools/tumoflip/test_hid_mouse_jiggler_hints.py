#!/usr/bin/env python3
"""Regression contracts for Mouse Jiggler Stealth interval hints."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
VIEW_PATH = "applications/system/hid_app/views/hid_mouse_jiggler_stealth.c"


def view_source() -> str:
    return (REPO_ROOT / VIEW_PATH).read_text(encoding="utf-8")


class MouseJigglerStealthHintsTest(unittest.TestCase):
    def test_interval_bounds_are_shared_with_input_logic(self) -> None:
        view = view_source()
        self.assertIn("#define INTERVAL_MIN_MINUTES 1", view)
        self.assertIn("#define INTERVAL_MAX_MINUTES 30", view)
        self.assertIn("model->min_interval > INTERVAL_MIN_MINUTES", view)
        self.assertIn("model->max_interval < INTERVAL_MAX_MINUTES", view)

    def test_hints_are_state_and_boundary_aware(self) -> None:
        view = view_source()
        self.assertGreaterEqual(view.count("if(!model->running)"), 2)
        self.assertIn("model->min_interval < model->max_interval", view)
        self.assertIn("model->max_interval > model->min_interval + 1", view)
        for icon in (
            "I_ButtonUp_7x4",
            "I_ButtonDown_7x4",
            "I_ButtonLeft_4x7",
            "I_ButtonRight_4x7",
        ):
            self.assertIn(icon, view)

    def test_mouse_delta_stays_within_the_signed_hid_report_range(self) -> None:
        view = view_source()
        self.assertIn("#include <stdint.h>", view)
        self.assertIn("int8_t move_x", view)
        self.assertIn("int8_t move_y", view)
        self.assertIn("2 * INT8_MAX + 1", view)
        self.assertNotIn("rand() % 2001", view)


if __name__ == "__main__":
    unittest.main()
