#!/usr/bin/env python3
"""Regression checks for ARF Custom Emulate and FastFAP output handling."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfCustomEmulateTest(unittest.TestCase):
    def test_custom_emulate_uses_protocol_labels_and_held_tx(self) -> None:
        scene = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/scenes/subghz_scene_car_emulate.c"
        ).read_text(encoding="utf-8")
        view = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/views/subghz_car_emulate.c"
        ).read_text(encoding="utf-8")
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn("subghz_button_labels_apply_protocol", scene)
        self.assertIn("subghz_button_labels_get_max_custom_btn", scene)
        self.assertNotIn("subghz_custom_btn_get_max()", scene)
        self.assertIn("car_emulate_restart_tx", scene)
        self.assertIn("subghz_block_generic_global.endless_tx = true", scene)
        self.assertIn("InputTypeRelease", view)
        self.assertIn('"helpers/subghz_button_labels.c"', manifest)

    def test_fastfap_objcopy_uses_distinct_input_and_output(self) -> None:
        source = (REPO_ROOT / "scripts/fastfap.py").read_text(encoding="utf-8")

        self.assertIn("def replace_file_with_retry", source)
        self.assertIn("current_fap_path", source)
        self.assertIn("patched_fap_path", source)
        self.assertIn("replace_file_with_retry(current_fap_path, fap_path)", source)


if __name__ == "__main__":
    unittest.main()
