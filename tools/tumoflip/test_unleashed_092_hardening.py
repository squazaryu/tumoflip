#!/usr/bin/env python3
"""Regression contracts for Unleashed #1104-#1108 hardening ports."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class Unleashed092HardeningTest(unittest.TestCase):
    def test_subghz_cleanup_is_null_safe_and_clears_transmitter(self) -> None:
        txrx = source("applications/main/subghz/helpers/subghz_txrx.c")
        error_path = txrx[txrx.index("if(ret != SubGhzTxRxStartTxStateOk)") :]
        transmitter = source("lib/subghz/transmitter.c")
        self.assertIn("if(!instance) return", transmitter)
        self.assertIn("subghz_transmitter_free(instance->transmitter)", error_path)
        self.assertIn("instance->transmitter = NULL", error_path)

    def test_binraw_encoder_checks_capacity_before_each_write(self) -> None:
        encoder = source("lib/subghz/blocks/encoder.c")
        self.assertIn("if(size_upload >= max_size_upload)", encoder)
        self.assertGreaterEqual(encoder.count("size_upload >= max_size_upload"), 2)

    def test_felica_lite_loader_rejects_invalid_block_counts(self) -> None:
        felica = source("lib/nfc/protocols/felica/felica.c")
        self.assertIn(
            "blocks_total > FELICA_BLOCKS_TOTAL_COUNT || blocks_read > blocks_total",
            felica,
        )
        self.assertIn(
            "Felica_oversized_lite.nfc",
            source("applications/debug/unit_tests/tests/nfc/nfc_test.c"),
        )
        self.assertTrue(
            (
                REPO_ROOT
                / "applications/debug/unit_tests/resources/unit_tests/nfc/"
                "Felica_oversized_lite.nfc"
            ).is_file()
        )

    def test_simple_array_compares_full_element_storage(self) -> None:
        simple_array = source("lib/toolbox/simple_array.c")
        self.assertIn("other->count * instance->config->type_size", simple_array)
        self.assertNotIn("memcmp(instance->data, other->data, other->count)", simple_array)

    def test_serial_expansion_rejects_end_sentinel(self) -> None:
        serial = source("targets/f7/furi_hal/furi_hal_serial_control.c")
        expansion = serial[serial.index("void furi_hal_serial_control_set_expansion_callback") :]
        self.assertIn("furi_check(serial_id < FuriHalSerialIdMax)", expansion)
        self.assertNotIn("furi_check(serial_id <= FuriHalSerialIdMax)", expansion)


if __name__ == "__main__":
    unittest.main()
