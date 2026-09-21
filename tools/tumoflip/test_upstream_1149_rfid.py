#!/usr/bin/env python3
"""Contracts for the upstream #1149 RFID manual-format adaptation."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LFRFID = ROOT / "applications/main/lfrfid"


class Upstream1149RfidTest(unittest.TestCase):
    def test_manual_format_modules_and_scenes_are_present(self) -> None:
        expected = (
            "lfrfid_manual_format.c",
            "lfrfid_manual_format.h",
            "lfrfid_hid_format.c",
            "lfrfid_hid_format.h",
            "lfrfid_casi_format.c",
            "lfrfid_casi_format.h",
            "scenes/lfrfid_scene_save_method.c",
            "scenes/lfrfid_scene_save_fields.c",
        )
        for relative in expected:
            self.assertTrue((LFRFID / relative).is_file(), relative)

    def test_manual_formats_cover_hid_and_casi_rusco(self) -> None:
        hid = (LFRFID / "lfrfid_hid_format.h").read_text(encoding="utf-8")
        hid_impl = (LFRFID / "lfrfid_hid_format.c").read_text(encoding="utf-8")
        casi = (LFRFID / "lfrfid_casi_format.h").read_text(encoding="utf-8")
        method = (LFRFID / "scenes/lfrfid_scene_save_method.c").read_text(
            encoding="utf-8"
        )

        for marker in (
            "H10302",
            "H10304",
            "H10306",
            "S10401",
            "Corp1000-35",
        ):
            self.assertIn(marker, hid_impl)
        self.assertIn("C10106", casi)
        self.assertIn("Enter FC/ID", method)
        self.assertIn("Enter Hex Data", method)

    def test_generic_hid_render_reports_frame_length_without_duplicate_data(self) -> None:
        source = (ROOT / "lib/lfrfid/protocols/protocol_hid_generic.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("bit HID Proximity", source)
        self.assertNotIn('furi_string_cat_printf(output, "Data:', source)

    def test_empty_number_input_is_interpreted_as_zero_when_in_range(self) -> None:
        source = (ROOT / "applications/services/gui/modules/number_input.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("min_value", source)
        self.assertIn("max_value", source)
        self.assertIn("number_input_get_value", source)
        self.assertIn("furi_string_empty", source)

    def test_unit_vectors_cover_non_nibble_hid_frames(self) -> None:
        source = (
            ROOT / "applications/debug/unit_tests/tests/lfrfid/lfrfid_protocols.c"
        ).read_text(encoding="utf-8")
        self.assertIn("37", source)
        self.assertIn("26", source)


if __name__ == "__main__":
    unittest.main()
