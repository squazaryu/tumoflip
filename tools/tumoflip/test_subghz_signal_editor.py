#!/usr/bin/env python3
"""Regression checks for the saved Signal Editor and protocol OFF filter."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SUBGHZ_ROOT = REPO_ROOT / "applications/main/subghz"


class SubGhzSignalEditorTest(unittest.TestCase):
    def test_saved_menu_exposes_editor_for_decoded_signals(self) -> None:
        source = (SUBGHZ_ROOT / "scenes/subghz_scene_saved_menu.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('!furi_string_equal_str(protocol, "RAW")', source)
        self.assertIn('"Signal Editor"', source)
        self.assertIn("SubGhzSceneSignalSettings", source)
        self.assertNotIn("FuriHalRtcFlagDebug", source)

    def test_signal_editor_uses_protocol_labels_and_rebuilds_capture(self) -> None:
        source = (SUBGHZ_ROOT / "scenes/subghz_scene_signal_settings.c").read_text(
            encoding="utf-8"
        )
        labels = (SUBGHZ_ROOT / "helpers/subghz_button_labels.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("subghz_scene_signal_settings_rebuild_save_reload", source)
        self.assertIn("subghz_block_generic_global_counter_override_set", source)
        self.assertIn("subghz_button_labels_apply_protocol", source)
        self.assertIn('"KIA/HYU V5"', labels)
        self.assertIn('"Nice FloR-S"', labels)

    def test_protocol_off_filter_keeps_protocol_pack_setting(self) -> None:
        header = (SUBGHZ_ROOT / "subghz_last_settings.h").read_text(encoding="utf-8")
        settings = (SUBGHZ_ROOT / "subghz_last_settings.c").read_text(encoding="utf-8")
        config = (SUBGHZ_ROOT / "scenes/subghz_scene_receiver_config.c").read_text(
            encoding="utf-8"
        )
        receiver = (SUBGHZ_ROOT / "scenes/subghz_scene_receiver.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("protocol_pack_group", header)
        self.assertIn("protocol_filter", header)
        self.assertIn('"ProtocolFilterOff"', settings)
        self.assertIn('"Protocol Pack"', config)
        self.assertIn('"Protocols"', config)
        self.assertIn("SubGhzSceneProtocolList", config)
        self.assertIn("subghz_last_settings_protocol_filter_contains", receiver)


if __name__ == "__main__":
    unittest.main()
