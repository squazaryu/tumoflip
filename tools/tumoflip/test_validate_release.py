#!/usr/bin/env python3

import copy
import os
import tempfile
import unittest
import zlib
from pathlib import Path

try:
    from .validate_release import (
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        crc32,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
    )
except ImportError:
    from validate_release import (
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        crc32,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
    )


REPO_ROOT = Path(__file__).resolve().parents[2]


class ValidateReleaseTest(unittest.TestCase):
    def test_protocol_pack_inventory_covers_active_arf_registry(self) -> None:
        self.assertEqual(len(PROTOCOL_PACKS), 24)
        self.assertIn("protocol_ford_v3.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_kia_v7.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_star_line.fal", PROTOCOL_PACKS)

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

    def test_find_objdump_falls_back_to_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            root.mkdir()
            bin_dir = Path(directory) / "bin"
            bin_dir.mkdir()
            objdump = bin_dir / "arm-none-eabi-objdump"
            objdump.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            objdump.chmod(0o755)

            old_path = os.environ.get("PATH", "")
            try:
                os.environ["PATH"] = f"{bin_dir}{os.pathsep}{old_path}"
                self.assertEqual(find_objdump(root), objdump)
            finally:
                os.environ["PATH"] = old_path

    def test_manifest_release_id_is_stable_and_content_addressed(self) -> None:
        manifest = {"schema": 2, "packages": {"base": [{"sha256": "abc"}]}}
        reordered = {"packages": {"base": [{"sha256": "abc"}]}, "schema": 2}
        changed = copy.deepcopy(manifest)
        changed["packages"]["base"][0]["sha256"] = "def"

        self.assertEqual(manifest_release_id(manifest), manifest_release_id(reordered))
        self.assertNotEqual(manifest_release_id(manifest), manifest_release_id(changed))

    def test_static_module_one_package_files_are_vendored(self) -> None:
        self.assertIn(
            "apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        for relative in MODULE_ONE_PACKAGE_FILES:
            if relative.endswith("esp32_wifi_marauder.fap"):
                path = REPO_ROOT / STATIC_SD_RESOURCES / relative
                self.assertTrue(path.is_file(), str(path))
                self.assertGreater(path.stat().st_size, 100 * 1024)


if __name__ == "__main__":
    unittest.main()
