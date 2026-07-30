#!/usr/bin/env python3
"""Regression contracts for preserving Sub-GHz RAW files after history TX."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


class SubGhzRawSaveGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.txrx = (
            REPO_ROOT / "applications/main/subghz/helpers/subghz_txrx.c"
        ).read_text(encoding="utf-8")
        cls.txrx_internal = (
            REPO_ROOT / "applications/main/subghz/helpers/subghz_txrx_i.h"
        ).read_text(encoding="utf-8")
        cls.subghz = (
            REPO_ROOT / "applications/main/subghz/subghz_i.c"
        ).read_text(encoding="utf-8")

    def test_tx_source_identity_is_initialized_and_recorded(self) -> None:
        self.assertIn("bool tx_from_internal_fff;", self.txrx_internal)
        self.assertIn("instance->tx_from_internal_fff = false;", self.txrx)

        start = self.txrx.index("SubGhzTxRxStartTxState subghz_txrx_tx_start")
        stop = self.txrx.index("subghz_txrx_stop(instance);", start)
        source = self.txrx.index(
            "instance->tx_from_internal_fff = (flipper_format == instance->fff_data);",
            start,
        )
        parse = self.txrx.index("flipper_format_rewind(flipper_format)", start)
        self.assertLess(stop, source)
        self.assertLess(source, parse)

    def test_dynamic_save_callback_is_gated_by_internal_format(self) -> None:
        tx_stop = self.txrx.index("static void subghz_txrx_tx_stop")
        callback = self.txrx.index("instance->need_save_callback(instance->need_save_context)", tx_stop)
        guard = self.txrx.rfind("if(instance->tx_from_internal_fff &&", tx_stop, callback)
        self.assertNotEqual(guard, -1)
        self.assertIn(
            "instance->decoder_result->protocol->type == SubGhzProtocolTypeDynamic",
            self.txrx[guard:callback],
        )

    def test_raw_header_cannot_reach_file_save(self) -> None:
        save = self.subghz.index("void subghz_save_to_file")
        load = self.subghz.index("bool subghz_load_protocol_from_file", save)
        handler = self.subghz[save:load]

        path_guard = handler.index("if(!subghz_path_is_file(subghz->file_path))")
        raw_read = handler.index('flipper_format_read_string(fff_data, "Protocol", protocol)')
        raw_guard = handler.index("if(!protocol_read || is_raw)")
        file_save = handler.index("subghz_save_protocol_to_file")
        self.assertLess(path_guard, raw_read)
        self.assertLess(raw_read, raw_guard)
        self.assertLess(raw_guard, file_save)
        self.assertIn('furi_string_equal(protocol, "RAW")', handler)

    def test_arf_profile_remains_outside_core_txrx_drift_contract(self) -> None:
        manifest = (
            REPO_ROOT / "tools/tumoflip/subghz_drift_manifest.txt"
        ).read_text(encoding="utf-8")
        self.assertNotIn("helpers/subghz_txrx.c", manifest)


if __name__ == "__main__":
    unittest.main()
