#!/usr/bin/env python3
"""Static contracts for transactional NFC checkpoint recovery."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


class NfcCheckpointTest(unittest.TestCase):
    def test_checkpoint_contract_is_bounded_and_transactional(self) -> None:
        app_header = (REPO_ROOT / "applications/main/nfc/nfc_app_i.h").read_text(
            encoding="utf-8"
        )
        app_source = (REPO_ROOT / "applications/main/nfc/nfc_app.c").read_text(
            encoding="utf-8"
        )
        device_source = (REPO_ROOT / "lib/nfc/nfc_device.c").read_text(
            encoding="utf-8"
        )
        start_scene = (
            REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_start.c"
        ).read_text(encoding="utf-8")
        saved_menu = (
            REPO_ROOT
            / "applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c"
        ).read_text(encoding="utf-8")

        for protocol in (
            "MF_CLASSIC",
            "MF_PLUS",
            "MF_ULTRALIGHT",
            "TYPE4",
        ):
            self.assertIn(f"NFC_APP_{protocol}_CHECKPOINT_PATH", app_header)
        for required in (
            "nfc_checkpoint_save",
            "nfc_checkpoint_clear",
            "nfc_checkpoint_exists",
            "nfc_checkpoint_path_for_protocol",
            ".tmp",
            ".bak",
            "nfc_device_save",
            "nfc_scene_start_recover_checkpoint",
            "Save recovered dump",
        ):
            self.assertIn(required, app_source + start_scene + saved_menu + device_source)

        self.assertIn("checkpoint_recovered", app_header)
        self.assertIn("NfcProtocolInvalid", app_source)
        self.assertIn("storage_common_rename", app_source + device_source)
        self.assertIn("nfc_checkpoint_clear", saved_menu)


if __name__ == "__main__":
    unittest.main()
