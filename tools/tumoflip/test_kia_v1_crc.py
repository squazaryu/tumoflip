#!/usr/bin/env python3
"""Regression checks for Kia v1 CRC metadata across bundled decoders."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
KIA_V1_SOURCES = (
    "lib/subghz/protocols/kia_v1.c",
    "applications_user/protopirate/protocols/kia_v1.c",
    "applications_user/rolljam_standalone/protocols/kia_v1.c",
)


def kia_v1_crc4(data: bytes, offset: int) -> int:
    crc = 0
    for byte in data:
        crc ^= (byte & 0x0F) ^ (byte >> 4)
    return (crc + offset) & 0x0F


class KiaV1CrcTest(unittest.TestCase):
    def test_known_frame_crc(self) -> None:
        payload = bytes((0x12, 0x34, 0x56, 0x78, 0x01, 0x23, 0x06))
        self.assertEqual(kia_v1_crc4(payload, 1), 0x0F)

    def test_all_decoders_store_only_crc_nibble(self) -> None:
        for relative_path in KIA_V1_SOURCES:
            with self.subTest(path=relative_path):
                source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
                self.assertIn("instance->crc = crc;", source)
                self.assertNotIn("instance->crc = cnt_high << 4 | crc;", source)


if __name__ == "__main__":
    unittest.main()
