#!/usr/bin/env python3
"""Regression contracts for malformed and partial NFC Type 4 Tag writes."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
LISTENER_PATH = (
    REPO_ROOT / "lib/nfc/protocols/type_4_tag/type_4_tag_listener_i.c"
)


def merge_nlen(current: int, offset: int, payload: bytes) -> tuple[int, bytes]:
    """Reference the two-byte big-endian NLEN merge used by the listener."""
    header = bytearray(current.to_bytes(2, "big"))
    write_len = min(len(header) - offset, len(payload))
    header[offset : offset + write_len] = payload[:write_len]
    return int.from_bytes(header, "big"), payload[write_len:]


class Type4TagWriteSafetyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.listener = LISTENER_PATH.read_text(encoding="utf-8")
        cls.write_handler = cls.listener[
            cls.listener.index("static Type4TagError type_4_tag_listener_iso_write") :
        ]

    def test_rejects_empty_write_before_dereferencing_data(self) -> None:
        empty_check = self.write_handler.index("if(lc == 0)")
        header_copy = self.write_handler.index("memcpy(&ndef_file_len_be[offset], data")
        self.assertLess(empty_check, header_copy)
        self.assertIn(
            "type_4_tag_bad_params_apdu",
            self.write_handler[empty_check:header_copy],
        )

    def test_partial_nlen_write_preserves_unwritten_byte(self) -> None:
        self.assertEqual(merge_nlen(0x0123, 0, b"\x02"), (0x0223, b""))
        self.assertEqual(merge_nlen(0x0123, 1, b"\x34"), (0x0134, b""))
        self.assertEqual(merge_nlen(0x0123, 1, b"\x34payload"), (0x0134, b"payload"))

        self.assertIn(
            "bit_lib_num_to_bytes_be("
            "ndef_file_len, sizeof(ndef_file_len_be), ndef_file_len_be)",
            self.write_handler,
        )
        self.assertIn(
            "const uint8_t write_len = MIN(sizeof(ndef_file_len_be) - offset, lc);",
            self.write_handler,
        )
        self.assertIn("data += write_len;", self.write_handler)
        self.assertIn("lc -= write_len;", self.write_handler)

    def test_bounds_checks_cover_apdu_and_reader_supplied_nlen(self) -> None:
        apdu_bounds = self.write_handler.index(
            "if(offset + lc > sizeof(uint16_t) + ndef_max_len)"
        )
        allocation = self.write_handler.index(
            "SimpleArray* ndef_data_temp = simple_array_alloc"
        )
        nlen_bounds = self.write_handler.index("if(ndef_file_len_new > ndef_max_len)")
        self.assertLess(apdu_bounds, allocation)
        self.assertLess(nlen_bounds, allocation)
        self.assertIn("type_4_tag_offset_error_apdu", self.write_handler[nlen_bounds:allocation])

    def test_listener_protocol_assertion_is_already_correct(self) -> None:
        listener = (
            REPO_ROOT / "lib/nfc/protocols/type_4_tag/type_4_tag_listener.c"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "furi_assert(event.protocol == NfcProtocolIso14443_4a);",
            listener,
        )
        self.assertNotIn(
            "furi_assert(event.protocol == NfcProtocolIso15693_3);",
            listener,
        )


if __name__ == "__main__":
    unittest.main()
