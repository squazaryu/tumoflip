#!/usr/bin/env python3
"""Regression contracts for the selected FeliCa Standard integration."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
FELICA_HEADER_SIZE = 10
FELICA_BLOCK_SIZE = 16


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def standard_request_is_valid(frame: bytes, write: bool) -> bool:
    """Model the fail-closed request-shape checks in the listener."""
    if len(frame) < FELICA_HEADER_SIZE + 1 or frame[0] != len(frame):
        return False

    service_count = frame[FELICA_HEADER_SIZE]
    if service_count == 0 or service_count > 16:
        return False

    offset = FELICA_HEADER_SIZE + 1 + service_count * 2
    if offset >= len(frame):
        return False

    block_count = frame[offset]
    offset += 1
    if block_count == 0 or block_count > (16 if write else 7):
        return False

    for _ in range(block_count):
        if offset >= len(frame):
            return False
        item_size = 2 if frame[offset] & 0x80 else 3
        if offset + item_size > len(frame):
            return False
        if item_size == 3 and frame[offset + 2] != 0:
            return False
        offset += item_size

    payload_size = block_count * FELICA_BLOCK_SIZE if write else 0
    return offset + payload_size == len(frame)


def make_standard_request(write: bool, block_count: int = 2) -> bytes:
    command = 0x08 if write else 0x06
    frame = bytearray([0, command, *range(8), 1, 0x09, 0x00, block_count])
    for block in range(block_count):
        frame.extend((0x80, block))
    if write:
        frame.extend(bytes(block_count * FELICA_BLOCK_SIZE))
    frame[0] = len(frame)
    return bytes(frame)


class FelicaStandardTest(unittest.TestCase):
    def test_valid_read_and_write_shapes(self) -> None:
        self.assertTrue(standard_request_is_valid(make_standard_request(False), False))
        self.assertTrue(standard_request_is_valid(make_standard_request(True), True))

    def test_truncated_block_list_and_write_payload_are_rejected(self) -> None:
        read = make_standard_request(False)
        write = make_standard_request(True)
        self.assertFalse(standard_request_is_valid(read[:-1], False))
        self.assertFalse(standard_request_is_valid(write[:-1], True))

    def test_unsupported_wide_block_number_is_rejected(self) -> None:
        frame = bytearray(make_standard_request(False, block_count=1))
        frame[-2:] = bytes((0x00, 0x01, 0x01))
        frame[0] = len(frame)
        self.assertFalse(standard_request_is_valid(bytes(frame), False))

    def test_listener_validates_before_dispatch(self) -> None:
        listener = source("lib/nfc/protocols/felica/felica_listener.c")
        listener_internal = source("lib/nfc/protocols/felica/felica_listener_i.c")
        self.assertIn("frame_size < 2U + FELICA_CRC_SIZE", listener)
        self.assertIn(
            "felica_listener_validate_request(instance, request, size)", listener
        )
        self.assertIn("felica_listener_validate_standard_request(", listener_internal)
        self.assertIn("offset + data_size == request_size", listener_internal)
        self.assertNotIn("block_count = FELICA_STANDARD_READ_BLOCK_MAX", listener)

    def test_standard_file_fixtures_and_version_three_tests_are_present(self) -> None:
        fixture_dir = (
            REPO_ROOT
            / "applications/debug/unit_tests/resources/unit_tests/nfc"
        )
        self.assertTrue((fixture_dir / "Felica_Standard_v2.nfc").is_file())
        self.assertTrue((fixture_dir / "Felica_Standard_with_keys.nfc").is_file())

        nfc_tests = source("applications/debug/unit_tests/tests/nfc/nfc_test.c")
        self.assertIn("felica_standard_file_test", nfc_tests)
        self.assertIn("felica_standard_v2", nfc_tests)
        self.assertIn("felica_standard_read", nfc_tests)


if __name__ == "__main__":
    unittest.main()
