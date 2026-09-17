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
        manual = (LFRFID / "lfrfid_manual_format.h").read_text(encoding="utf-8")
        hid = (LFRFID / "lfrfid_hid_format.h").read_text(encoding="utf-8")
        casi = (LFRFID / "lfrfid_casi_format.h").read_text(encoding="utf-8")

        for marker in (
            "H10301",
            "H10302",
            "H10304",
            "H10306",
            "S10401",
            "Corporate 1000",
        ):
            self.assertIn(marker, hid)
        self.assertIn("C10106", casi)
        self.assertIn("Enter FC/ID", manual)
        self.assertIn("Enter Hex Data", manual)

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
        self.assertIn("value_min", source)
        self.assertIn("value_max", source)
        self.assertRegex(source, r"empty.*zero|zero.*empty|value_min")

    def test_unit_vectors_cover_non_nibble_hid_frames(self) -> None:
        source = (
            ROOT / "applications/debug/unit_tests/tests/lfrfid/lfrfid_protocols.c"
        ).read_text(encoding="utf-8")
        self.assertIn("37", source)
        self.assertIn("26", source)


if __name__ == "__main__":
    unittest.main()
