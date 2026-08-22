#!/usr/bin/env python3
"""Regression contracts for partial and legacy MIFARE Classic card parsing."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SUPPORTED_CARDS = REPO_ROOT / "applications/main/nfc/plugins/supported_cards"
DISPATCHER = REPO_ROOT / "applications/main/nfc/helpers/nfc_supported_cards.c"
UTILITY = SUPPORTED_CARDS / "mf_classic_parser_util.h"
SOCIAL_MOSCOW = SUPPORTED_CARDS / "social_moscow.c"

GUARDED_PARSERS = {
    "bambu.c",
    "banapass.c",
    "bip.c",
    "charliecard.c",
    "csc.c",
    "hworld.c",
    "kazan.c",
    "metromoney.c",
    "microel.c",
    "mizip.c",
    "plantain.c",
    "saflok.c",
    "sevppk_tk.c",
    "sk_tk.c",
    "smartrider.c",
    "social_moscow.c",
    "szppk_so.c",
    "two_cities.c",
    "washcity.c",
}


def parser_block_has_data(read_mask: bool, block: bytes) -> bool:
    """Mirror the supported-card contract without importing firmware C code."""
    return read_mask or any(block)


class MifareClassicParserIntegrityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dispatcher = DISPATCHER.read_text(encoding="utf-8")
        cls.utility = UTILITY.read_text(encoding="utf-8")
        cls.social_moscow = SOCIAL_MOSCOW.read_text(encoding="utf-8")

    def test_each_candidate_starts_with_an_empty_output_buffer(self) -> None:
        parse_loop = self.dispatcher[self.dispatcher.index("bool nfc_supported_cards_parse(") :]
        reset = parse_loop.index("furi_string_reset(parsed_data);")
        parse = parse_loop.index("plugin->parse(device, parsed_data)")
        self.assertLess(reset, parse)

    def test_legacy_nonzero_blocks_remain_renderable(self) -> None:
        self.assertIn("mf_classic_get_total_block_num(data->type)", self.utility)
        self.assertIn("mf_classic_is_block_read(data, block_num)", self.utility)
        self.assertIn("data->block[block_num].data[i] != 0", self.utility)

        self.assertTrue(parser_block_has_data(True, bytes(16)))
        self.assertFalse(parser_block_has_data(False, bytes(16)))
        self.assertTrue(parser_block_has_data(False, bytes([1]) + bytes(15)))

    def test_all_affected_parsers_use_the_shared_block_contract(self) -> None:
        self.assertEqual(len(GUARDED_PARSERS), 19)
        for filename in GUARDED_PARSERS:
            source = (SUPPORTED_CARDS / filename).read_text(encoding="utf-8")
            self.assertIn('#include "mf_classic_parser_util.h"', source, filename)
            self.assertIn("mf_classic_parser_block_has_data", source, filename)

    def test_social_moscow_rejects_blank_identity_and_marks_missing_omc(self) -> None:
        self.assertIn("if(number == 0)", self.social_moscow)
        self.assertIn("mf_classic_parser_block_has_data(data, 21)", self.social_moscow)
        self.assertIn('"OMC: Unknown\\n"', self.social_moscow)
        self.assertIsNone(
            re.search(r"mf_classic_is_block_read\(data,\s*(?:21|60)\)", self.social_moscow)
        )


if __name__ == "__main__":
    unittest.main()
