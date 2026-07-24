#!/usr/bin/env python3
"""Regression checks for read-only Mosgortrans subscription parsing."""

from __future__ import annotations

import datetime as dt
import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PARSER_SOURCE = (
    REPO_ROOT
    / "applications/main/nfc/api/mosgortrans/mosgortrans_util.c"
)


def set_bits(data: bytearray, position: int, length: int, value: int) -> None:
    if value < 0 or value >= 1 << length:
        raise ValueError("value does not fit in field")

    for offset in range(length):
        bit = (value >> (length - offset - 1)) & 1
        byte_index = (position + offset) // 8
        shift = 7 - ((position + offset) % 8)
        data[byte_index] = (data[byte_index] & ~(1 << shift)) | (bit << shift)


def get_bits(data: bytes, position: int, length: int) -> int:
    value = 0
    for offset in range(length):
        byte_index = (position + offset) // 8
        shift = 7 - ((position + offset) % 8)
        value = (value << 1) | ((data[byte_index] >> shift) & 1)
    return value


def days_since_1991(date: dt.date) -> int:
    return (date - dt.date(1991, 12, 31)).days


def extract_case(source: str, layout: str) -> str:
    start = source.index(f"case {layout}:")
    next_case = source.find("\n    case ", start + 1)
    default_case = source.find("\n    default:", start + 1)
    candidates = [index for index in (next_case, default_case) if index != -1]
    return source[start : min(candidates) if candidates else len(source)]


class MosgortransSubscriptionsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.source = PARSER_SOURCE.read_text(encoding="utf-8")

    def test_f0b_fixture_contains_subscription_dates(self) -> None:
        block = bytearray(32)
        expected_number = 123456789
        expected_from = dt.date(2026, 7, 1)
        expected_to = dt.date(2026, 9, 30)

        set_bits(block, 0x00, 10, 0x106)
        set_bits(block, 0x14, 32, expected_number)
        set_bits(block, 0x34, 14, 0x3C0B)
        set_bits(block, 0x42, 16, days_since_1991(expected_from))
        set_bits(block, 0x52, 16, days_since_1991(expected_to))

        self.assertEqual(get_bits(block, 0x00, 10), 0x106)
        self.assertEqual(get_bits(block, 0x14, 32), expected_number)
        self.assertEqual(get_bits(block, 0x34, 14), 0x3C0B)
        self.assertEqual(
            dt.date(1991, 12, 31)
            + dt.timedelta(days=get_bits(block, 0x42, 16)),
            expected_from,
        )
        self.assertEqual(
            dt.date(1991, 12, 31)
            + dt.timedelta(days=get_bits(block, 0x52, 16)),
            expected_to,
        )

    def test_f0b_decoder_is_called_before_rendering(self) -> None:
        layout_case = extract_case(self.source, "0x3C0B")
        decoder = layout_case.index("parse_layout_F0B(&data_block, block);")
        number = layout_case.index('"Number: %010lu')
        valid_from = layout_case.index('"Valid from:')
        valid_to = layout_case.index('"Valid to:')

        self.assertLess(decoder, number)
        self.assertLess(decoder, valid_from)
        self.assertLess(decoder, valid_to)

    def test_empty_recognized_ticket_remains_visible(self) -> None:
        layout_case = extract_case(self.source, "0x02")
        no_ticket = re.search(
            r'if\(data_block\.valid_from_date == 0 \|\| '
            r'data_block\.valid_to_date == 0\) \{'
            r'.*?furi_string_cat\(result, "\\e#No ticket"\);'
            r'.*?return true;',
            layout_case,
            re.DOTALL,
        )
        self.assertIsNotNone(no_ticket)

    def test_unknown_transport_layout_is_reported_read_only(self) -> None:
        default_case = self.source[self.source.rindex("    default:") :]
        self.assertIn('"Ticket data detected\\nLayout: %04X\\nValidity: not decoded"', default_case)
        self.assertIn("return true;", default_case)
        self.assertNotIn("result = NULL;", default_case)


if __name__ == "__main__":
    unittest.main()
