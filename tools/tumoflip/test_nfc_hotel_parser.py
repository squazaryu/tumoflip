#!/usr/bin/env python3
"""Regression contracts for passive Onity and VingCard NFC identification."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_FAM = REPO_ROOT / "applications/main/nfc/application.fam"
HOTEL_PARSER = REPO_ROOT / "applications/main/nfc/plugins/supported_cards/hotels.c"
SAFLOK_PARSER = REPO_ROOT / "applications/main/nfc/plugins/supported_cards/saflok.c"
MIFARE_DICTIONARY = REPO_ROOT / "applications/main/nfc/resources/nfc/assets/mf_classic_dict.nfc"


def function_body(source: str, signature: str) -> str:
    try:
        start = source.index(signature)
    except ValueError:
        return ""

    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"unterminated function: {signature}")


class NfcHotelParserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app_config = APP_FAM.read_text(encoding="utf-8")
        cls.hotel_source = HOTEL_PARSER.read_text(encoding="utf-8") if HOTEL_PARSER.exists() else ""
        cls.saflok_source = SAFLOK_PARSER.read_text(encoding="utf-8")
        cls.dictionary = MIFARE_DICTIONARY.read_text(encoding="utf-8")
        cls.parse = function_body(cls.hotel_source, "static bool hotels_parse(")
        cls.key_match = function_body(cls.hotel_source, "static bool hotels_key_matches(")

    def test_parser_is_registered_as_nfc_mifare_classic_plugin(self) -> None:
        app = re.search(r'App\(\s*appid="hotels_parser",(?P<body>.*?)\n\)', self.app_config, re.S)
        self.assertIsNotNone(app)
        self.assertIn('apptype=FlipperAppType.PLUGIN', app.group("body"))
        self.assertIn('requires=["nfc"]', app.group("body"))
        self.assertIn('sources=["plugins/supported_cards/hotels.c"]', app.group("body"))
        self.assertIn('.protocol = NfcProtocolMfClassic', self.hotel_source)

    def test_only_missing_hotel_families_are_added_and_saflok_detail_is_preserved(self) -> None:
        self.assertIn("Onity", self.hotel_source)
        self.assertIn("VingCard", self.hotel_source)
        self.assertNotIn("Saflok", self.hotel_source)
        self.assertIn('"saflok_mfc_parser"', self.app_config)
        self.assertIn('Saflok MFC 1K Card', self.saflok_source)

    def test_matching_requires_valid_classic_type_and_a_recovered_sector_key(self) -> None:
        self.assertIn("MfClassicTypeNum", self.parse)
        self.assertIn("mf_classic_get_total_sectors_num", self.parse)
        self.assertIn("NfcProtocolMfClassic", self.parse)
        self.assertIn("mf_classic_is_key_found(data, sector, key_type)", self.key_match)
        self.assertIn("mf_classic_get_key(data, sector, key_type)", self.key_match)
        self.assertIn("memcmp", self.key_match)

        mask_check = self.key_match.index("mf_classic_is_key_found(data, sector, key_type)")
        key_read = self.key_match.index("mf_classic_get_key(data, sector, key_type)")
        self.assertLess(mask_check, key_read)
        self.assertIn("hotels_key_matches(data, 1, MfClassicKeyTypeA, onity_key)", self.parse)
        self.assertIn("hotels_key_matches(data, 2, MfClassicKeyTypeB, vingcard_key)", self.parse)

    def test_parser_uses_the_existing_mifare_dictionary_keys(self) -> None:
        self.assertIn("0x8A, 0x19, 0xD4, 0x0C, 0xF2, 0xB5", self.hotel_source)
        self.assertIn("0x00, 0x00, 0x01, 0x4B, 0x5C, 0x31", self.hotel_source)
        self.assertIn("8A19D40CF2B5", self.dictionary)
        self.assertIn("0000014B5C31", self.dictionary)

    def test_unknown_cards_are_rejected_and_overlapping_matches_are_not_hidden(self) -> None:
        self.assertIn("if(!onity && !vingcard)", self.parse)
        self.assertIn("Possible system: Onity", self.parse)
        self.assertIn("Possible system: VingCard", self.parse)
        self.assertIn("Possible systems:", self.parse)
        self.assertIn("Possible systems:\\nOnity\\nVingCard", self.parse)

    def test_plugin_is_parse_only_and_never_starts_a_card_read(self) -> None:
        self.assertIn(".verify = NULL", self.hotel_source)
        self.assertIn(".read = NULL", self.hotel_source)
        self.assertNotRegex(self.hotel_source, r"mf_classic_poller_sync_(?:read|auth)")


if __name__ == "__main__":
    unittest.main()
