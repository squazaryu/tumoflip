#!/usr/bin/env python3
"""Regression checks for ARF Custom Emulate and FastFAP output handling."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfCustomEmulateTest(unittest.TestCase):
    HELD_TX_PROTOCOLS = (
        "fiat_spa",
        "ford_v0",
        "ford_v1",
        "ford_v2",
        "kia_v0",
        "kia_v1",
        "kia_v2",
        "kia_v3_v4",
        "kia_v5",
        "kia_v6",
        "kia_v7",
        "land_rover_v0",
        "mazda_v0",
        "mitsubishi_v0",
        "porsche_cayenne",
        "psa",
        "scher_khan",
        "sheriff_cfm",
        "star_line",
        "subaru",
        "suzuki",
        "vag",
    )

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

    def test_all_arf_encoders_keep_transmitting_while_held(self) -> None:
        for protocol in self.HELD_TX_PROTOCOLS:
            with self.subTest(protocol=protocol):
                source = (
                    REPO_ROOT / f"lib/subghz/protocols/{protocol}.c"
                ).read_text(encoding="utf-8")
                self.assertIn("subghz_block_generic_global.endless_tx", source)

    def test_receiver_full_dpad_handoff_stops_rx_before_transmitter(self) -> None:
        event_header = (
            REPO_ROOT
            / "applications/main/subghz/helpers/subghz_custom_event.h"
        ).read_text(encoding="utf-8")
        scene = (
            REPO_ROOT
            / "applications/main/subghz/scenes/subghz_scene_receiver_info.c"
        ).read_text(encoding="utf-8")

        self.assertIn("SubGhzCustomEventSceneReceiverInfoTxFullDpad", event_header)
        self.assertIn('GuiButtonTypeLeft,\n                "Full"', scene)
        handoff = scene.split(
            "event.event == SubGhzCustomEventSceneReceiverInfoTxFullDpad", 1
        )[1]
        self.assertIn("stream_copy_full(source, destination)", handoff)
        stop_index = handoff.index("subghz_txrx_stop(subghz->txrx);")
        copy_index = handoff.index("stream_copy_full(source, destination)")
        transmitter_index = handoff.index(
            "scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);"
        )
        self.assertLess(stop_index, copy_index)
        self.assertLess(stop_index, transmitter_index)

    def test_fastfap_objcopy_uses_distinct_input_and_output(self) -> None:
        source = (REPO_ROOT / "scripts/fastfap.py").read_text(encoding="utf-8")

        self.assertIn("def replace_file_with_retry", source)
        self.assertIn("current_fap_path", source)
        self.assertIn("patched_fap_path", source)
        self.assertIn("replace_file_with_retry(current_fap_path, fap_path)", source)
        self.assertIn("shutil.copy2(source, target)", source)
        self.assertIn("dir=os.path.dirname(os.path.abspath(fap_path))", source)


if __name__ == "__main__":
    unittest.main()
