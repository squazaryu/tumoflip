#!/usr/bin/env python3
"""Regression contracts for compatible NFC Type 4 Tag file selection."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
TYPE4_ROOT = REPO_ROOT / "lib/nfc/protocols/type_4_tag"
HEADER_PATH = TYPE4_ROOT / "type_4_tag_i.h"
POLLER_PATH = TYPE4_ROOT / "type_4_tag_poller_i.c"
LISTENER_PATH = TYPE4_ROOT / "type_4_tag_listener_i.c"
POLLER_STATE_PATH = TYPE4_ROOT / "type_4_tag_poller.c"


def select_file_apdu(file_id: int) -> bytes:
    """Reference ISO SELECT FILE APDU emitted by the Type 4 poller."""
    if not 0 <= file_id <= 0xFFFF:
        raise ValueError("file_id must fit in two bytes")
    return bytes((0x00, 0xA4, 0x00, 0x0C, 0x02)) + file_id.to_bytes(2, "big")


class Type4TagSelectCompatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER_PATH.read_text(encoding="utf-8")
        cls.poller = POLLER_PATH.read_text(encoding="utf-8")
        cls.listener = LISTENER_PATH.read_text(encoding="utf-8")
        cls.poller_state = POLLER_STATE_PATH.read_text(encoding="utf-8")

    def test_poller_selects_cc_and_ndef_with_general_file_id_p1(self) -> None:
        self.assertEqual(select_file_apdu(0xE103), bytes.fromhex("00 A4 00 0C 02 E1 03"))
        self.assertEqual(select_file_apdu(0xE104), bytes.fromhex("00 A4 00 0C 02 E1 04"))
        self.assertIn("TYPE_4_TAG_ISO_SELECT_P1_BY_EF_ID       (0x00)", self.header)
        self.assertNotIn("TYPE_4_TAG_ISO_SELECT_P1_BY_EF_ID (0x02)", self.header)

        select_file = self.poller[
            self.poller.index("type_4_tag_poller_iso_select_file") :
            self.poller.index("static Type4TagError type_4_tag_poller_iso_read")
        ]
        expected_fields = (
            "TYPE_4_TAG_ISO_SELECT_CMD",
            "TYPE_4_TAG_ISO_SELECT_P1_BY_EF_ID",
            "TYPE_4_TAG_ISO_SELECT_P2_EMPTY",
            "sizeof(file_id)",
        )
        positions = [select_file.index(field) for field in expected_fields]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("bit_lib_num_to_bytes_be(file_id, sizeof(file_id), file_id_be);", select_file)

    def test_read_sequence_selects_application_then_cc_then_ndef(self) -> None:
        handler = self.poller_state[
            self.poller_state.index("type_4_tag_poller_read_handler") :
        ]
        steps = (
            "Type4TagPollerStateSelectApplication",
            "Type4TagPollerStateReadCapabilityContainer",
            "Type4TagPollerStateReadNdefMessage",
        )
        positions = [handler.index(step) for step in steps]
        self.assertEqual(positions, sorted(positions))
        self.assertIn(
            "type_4_tag_poller_iso_select_file(instance, TYPE_4_TAG_T4T_CC_EF_ID)",
            self.poller,
        )
        self.assertIn(
            "type_4_tag_poller_iso_select_file(instance, instance->data->ndef_file_id)",
            self.poller,
        )

    def test_listener_keeps_legacy_df_scoped_selector_compatibility(self) -> None:
        self.assertIn("TYPE_4_TAG_ISO_SELECT_P1_BY_EF_ID_IN_DF (0x02)", self.header)
        select_handler = self.listener[
            self.listener.index("static Type4TagError type_4_tag_listener_iso_select") :
            self.listener.index("static Type4TagError type_4_tag_listener_iso_read")
        ]
        self.assertIn("p1 == TYPE_4_TAG_ISO_SELECT_P1_BY_ID", select_handler)
        self.assertIn("p1 == TYPE_4_TAG_ISO_SELECT_P1_BY_EF_ID_IN_DF", select_handler)

    def test_apdu_status_word_is_required_and_removed_from_payload(self) -> None:
        transaction = self.poller[
            self.poller.index("Type4TagError type_4_tag_apdu_trx") :
            self.poller.index("static Type4TagError type_4_tag_poller_iso_select_name")
        ]
        self.assertIn("response_len < TYPE_4_TAG_ISO_STATUS_LEN", transaction)
        self.assertIn("TYPE_4_TAG_ISO_STATUS_SUCCESS", transaction)
        self.assertIn("bit_buffer_set_size_bytes(rx_buf, response_len - 2);", transaction)
        self.assertIn("memcmp(status, success, sizeof(status)) == 0", transaction)
        self.assertIn("return Type4TagErrorApduFailed;", transaction)


if __name__ == "__main__":
    unittest.main()
