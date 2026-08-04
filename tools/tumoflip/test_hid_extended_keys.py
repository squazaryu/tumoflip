#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class HidExtendedKeysTest(unittest.TestCase):
    def test_usb_and_ble_descriptors_cover_existing_extended_keys(self) -> None:
        usb = (
            REPO_ROOT / "targets/f7/furi_hal/furi_hal_usb_hid.c"
        ).read_text(encoding="utf-8")
        ble = (
            REPO_ROOT / "lib/ble_profile/extra_profiles/hid_profile.c"
        ).read_text(encoding="utf-8")

        for source in (usb, ble):
            self.assertIn("HID_RI_LOGICAL_MAXIMUM(16, 255)", source)
            self.assertIn("HID_RI_USAGE_MAXIMUM(16, 255)", source)
            self.assertNotIn("HID_LOGICAL_MAXIMUM(101)", source)
            self.assertNotIn("HID_USAGE_MAXIMUM(101)", source)

    def test_native_and_js_badusb_already_expose_f13_through_f24(self) -> None:
        native = (
            REPO_ROOT / "applications/main/bad_usb/helpers/ducky_script_keycodes.c"
        ).read_text(encoding="utf-8")
        javascript = (
            REPO_ROOT / "applications/system/js_app/modules/js_badusb.c"
        ).read_text(encoding="utf-8")

        for key_number in range(13, 25):
            key = f'"F{key_number}"'
            self.assertIn(key, native)
            self.assertIn(key, javascript)

    def test_japanese_layout_is_intentionally_not_added(self) -> None:
        layout = (
            REPO_ROOT
            / "applications/main/bad_usb/resources/badusb/assets/layouts/ja-JP.kl"
        )
        self.assertFalse(layout.exists())


if __name__ == "__main__":
    unittest.main()
