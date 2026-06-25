#!/usr/bin/env python3

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

try:
    from .make_asset_pack import (
        AssetPackError,
        DEFAULT_ICON_SOURCES,
        ext_asset_pack_root,
        build_asset_pack,
    )
except ImportError:
    from make_asset_pack import (
        AssetPackError,
        DEFAULT_ICON_SOURCES,
        REPO_ROOT,
        ext_asset_pack_root,
        build_asset_pack,
    )
else:
    from .make_asset_pack import REPO_ROOT


class MakeAssetPackTest(unittest.TestCase):
    def test_builds_default_pack_layout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "asset_packs"
            written = build_asset_pack("test_pack", root, DEFAULT_ICON_SOURCES)

            self.assertEqual(
                sorted(path.relative_to(root).as_posix() for path in written),
                [
                    "active.txt",
                    "test_pack/Icons/ARFTools_14.bmx",
                    "test_pack/Icons/ModuleOne_14.bmx",
                ],
            )
            self.assertEqual((root / "active.txt").read_text(encoding="utf-8"), "test_pack\n")

            for filename in ("ModuleOne_14.bmx", "ARFTools_14.bmx"):
                data = (root / "test_pack/Icons" / filename).read_bytes()
                width, height = struct.unpack("<II", data[:8])
                self.assertEqual((width, height), (14, 14))
                self.assertGreater(len(data[8:]), 0)
                self.assertLessEqual(len(data[8:]), 256)

    def test_rejects_unsafe_pack_names(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for pack_name in ("", "../bad", "bad.name", "bad/name", "bad name"):
                with self.assertRaises(AssetPackError, msg=pack_name):
                    build_asset_pack(pack_name, root, DEFAULT_ICON_SOURCES)

    def test_ext_root_maps_to_expected_sd_path(self) -> None:
        self.assertEqual(
            ext_asset_pack_root(Path("/Volumes/FLIPPER")).as_posix(),
            "/Volumes/FLIPPER/apps_data/tumoflip/asset_packs",
        )

    def test_cli_reports_invalid_pack_name(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "tools/tumoflip/make_asset_pack.py"),
                "bad/name",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("pack name must contain only", result.stderr)


if __name__ == "__main__":
    unittest.main()
