#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class Api88CcidMigrationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.f7_api = (REPO_ROOT / "targets/f7/api_symbols.csv").read_text(
            encoding="utf-8"
        )
        self.f18_api = (REPO_ROOT / "targets/f18/api_symbols.csv").read_text(
            encoding="utf-8"
        )

    def test_f7_uses_experimental_api_88_5_and_f18_stays_88_0(self) -> None:
        self.assertIn("Version,+,88.5,,", self.f7_api)
        self.assertIn("Version,+,88.0,,", self.f18_api)

    def test_legacy_ccid_hal_is_not_exported(self) -> None:
        for api in (self.f7_api, self.f18_api):
            self.assertNotIn("furi_hal_usb_ccid", api)
            self.assertNotIn("Variable,+,usb_ccid,", api)

        self.assertFalse(
            (REPO_ROOT / "targets/f7/furi_hal/furi_hal_usb_ccid.c").exists()
        )
        self.assertFalse(
            (REPO_ROOT / "targets/furi_hal_include/furi_hal_usb_ccid.h").exists()
        )

    def test_ccid_debug_app_owns_its_usb_backend(self) -> None:
        app_dir = REPO_ROOT / "applications/debug/ccid_test"
        source = (app_dir / "ccid_test_app.c").read_text(encoding="utf-8")
        backend = (app_dir / "ccid_usb.c").read_text(encoding="utf-8")
        header = (app_dir / "ccid_usb.h").read_text(encoding="utf-8")

        self.assertIn("ccid_usb_interface", source)
        self.assertIn("ccid_usb_insert_smartcard", source)
        self.assertIn("FuriHalUsbInterface ccid_usb_interface", backend)
        self.assertIn("extern FuriHalUsbInterface ccid_usb_interface", header)
        combined = source + backend + header
        self.assertNotIn("#include <furi_hal_usb_ccid.h>", combined)
        self.assertNotIn("furi_hal_usb_ccid_insert_smartcard(", combined)
        self.assertNotIn("furi_hal_usb_ccid_remove_smartcard(", combined)
        self.assertNotIn("furi_hal_usb_ccid_set_callbacks(", combined)
        self.assertNotIn("extern FuriHalUsbInterface usb_ccid", combined)


if __name__ == "__main__":
    unittest.main()
