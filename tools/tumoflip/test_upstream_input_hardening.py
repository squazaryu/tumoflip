"""Regression contracts for bounded external data and stable LF RFID IDs."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class UpstreamInputHardeningTests(unittest.TestCase):
    def test_nested_smart_posters_are_bounded_and_depth_is_restored(self) -> None:
        text = source("applications/main/nfc/plugins/supported_cards/ndef.c")
        body = function_body(text, "bool ndef_parse_record(")

        self.assertTrue("NDEF_SMART_POSTER_MAX_DEPTH" in text, "missing depth limit")
        self.assertTrue("smart_poster_depth" in text, "missing per-parse depth state")
        self.assertTrue(
            "ndef->smart_poster_depth >= NDEF_SMART_POSTER_MAX_DEPTH" in body,
            "missing nested-record guard",
        )
        self.assertLess(
            body.index("ndef->smart_poster_depth++"),
            body.index("ndef_parse_message(ndef, pos, len, 0, true)"),
        )
        self.assertIn("ndef->smart_poster_depth--", body)

    def test_unknown_ibutton_protocol_is_rejected_before_group_lookup(self) -> None:
        text = source("lib/ibutton/ibutton_protocols.c")
        body = function_body(text, "bool ibutton_protocols_load(")

        self.assertTrue(
            "if(id == iButtonProtocolIdInvalid) break;" in body,
            "unknown protocol must fail before group lookup",
        )
        guard = body.index("if(id == iButtonProtocolIdInvalid) break;")
        self.assertLess(guard, body.index("ibutton_key_set_protocol_id(key, id)"))
        self.assertLess(guard, body.index("GET_PROTOCOL_GROUP(id)"))

    def test_desfire_access_rights_fit_the_destination_before_reading(self) -> None:
        text = source("lib/nfc/protocols/mf_desfire/mf_desfire_i.c")
        body = function_body(text, "bool mf_desfire_file_settings_load(")

        self.assertTrue(
            "access_rights_len > sizeof(data->access_rights)" in body,
            "saved access rights must fit the fixed destination",
        )
        guard = body.index("access_rights_len > sizeof(data->access_rights)")
        self.assertLess(guard, body.index("(uint8_t*)&data->access_rights"))

    def test_tar_read_errors_cannot_become_unsigned_write_lengths(self) -> None:
        text = source("lib/toolbox/tar/tar_archive.c")
        body = function_body(text, "static bool archive_extract_current_file(")

        self.assertTrue("readcnt <= 0" in body, "negative TAR reads must fail")
        self.assertLess(body.index("readcnt <= 0"), body.index("storage_file_write(out_file"))

    def test_indala224_does_not_renumber_existing_protocols(self) -> None:
        text = source("lib/lfrfid/protocols/lfrfid_protocols.h")
        match = re.search(r"typedef enum \{(.*?)\} LFRFIDProtocol;", text, re.DOTALL)
        self.assertIsNotNone(match)
        protocol_ids = re.findall(r"\bLFRFIDProtocol\w+\b", match.group(1))

        self.assertEqual(protocol_ids.index("LFRFIDProtocolIOProxXSF"), 7)
        self.assertEqual(protocol_ids.index("LFRFIDProtocolNoralsy"), 23)
        self.assertEqual(protocol_ids.index("LFRFIDProtocolIndala224"), 24)


if __name__ == "__main__":
    unittest.main()
