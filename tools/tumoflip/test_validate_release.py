#!/usr/bin/env python3

import copy
import tempfile
import unittest
import zlib
from pathlib import Path

try:
    from .validate_release import (
        crc32,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
    )
except ImportError:
    from validate_release import (
        crc32,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
    )


class ValidateReleaseTest(unittest.TestCase):
    def test_parse_fuf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "update.fuf"
            path.write_text(
                "Filetype: Test\n# comment\nVersion: 2\nRadio address: 00 70 0D 08\n",
                encoding="utf-8",
            )
            self.assertEqual(parse_fuf(path)["Version"], "2")
            self.assertEqual(parse_fuf(path)["Radio address"], "00 70 0D 08")

    def test_little_endian_hex(self) -> None:
        self.assertEqual(little_endian_hex("00 70 0D 08"), 0x080D7000)

    def test_crc32(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.bin"
            path.write_bytes(b"tumoflip")
            self.assertEqual(crc32(path), zlib.crc32(b"tumoflip") & 0xFFFFFFFF)

    def test_manifest_release_id_is_stable_and_content_addressed(self) -> None:
        manifest = {"schema": 2, "packages": {"base": [{"sha256": "abc"}]}}
        reordered = {"packages": {"base": [{"sha256": "abc"}]}, "schema": 2}
        changed = copy.deepcopy(manifest)
        changed["packages"]["base"][0]["sha256"] = "def"

        self.assertEqual(manifest_release_id(manifest), manifest_release_id(reordered))
        self.assertNotEqual(manifest_release_id(manifest), manifest_release_id(changed))


if __name__ == "__main__":
    unittest.main()
