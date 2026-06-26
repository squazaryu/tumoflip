#!/usr/bin/env python3

from hashlib import sha256
from pathlib import Path
import tempfile
import unittest

import fbt_options

from tools.tumoflip.generate_update_splash import generate_slideshow
from tools.tumoflip.sync_update_splash import sync_update_splash, version_from_dist_suffix


REPO_ROOT = Path(__file__).resolve().parents[2]
SPLASH_DIR = REPO_ROOT / "assets/slideshow/tumoflip_update"
STATIC_FRAME_SHA256 = {
    "frame_01.png": "9b42c3774f389f168c2b072705dc7c516087009b15f66b4c6b8745bdfd615bd0",
    "frame_02.png": "fe93ce94fda74729938823e9896878fafc70098ebeb2b1919b33388a11645c56",
    "frame_03.png": "71cd8aba43d2f63b02343282e0896a01f0cc0fa43b25872c48a32ec3a248f94a",
}


class UpdateSplashTest(unittest.TestCase):
    def test_update_splash_matches_current_firmware_version(self) -> None:
        prefix = "tmwhflpprarf"
        self.assertTrue(fbt_options.DIST_SUFFIX.startswith(prefix))
        version = fbt_options.DIST_SUFFIX.removeprefix(prefix)

        with tempfile.TemporaryDirectory() as directory:
            expected_dir = Path(directory)
            expected_frames = generate_slideshow("TMWHFLPPRARF", version, expected_dir)

            for expected in expected_frames:
                actual = SPLASH_DIR / expected.name
                self.assertEqual(actual.read_bytes(), expected.read_bytes())

    def test_update_splash_has_expected_pages(self) -> None:
        frames = sorted(path.name for path in SPLASH_DIR.glob("frame_*.png"))
        self.assertEqual(
            frames,
            ["frame_00.png", "frame_01.png", "frame_02.png", "frame_03.png"],
        )

    def test_static_pages_match_089_015_layout(self) -> None:
        for name, expected_hash in STATIC_FRAME_SHA256.items():
            self.assertEqual(
                sha256((SPLASH_DIR / name).read_bytes()).hexdigest(),
                expected_hash,
            )

    def test_sync_update_splash_removes_stale_frames(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            splash_dir = Path(directory)
            (splash_dir / "frame_04.png").write_bytes(b"stale")

            self.assertTrue(
                sync_update_splash(
                    splash_dir,
                    dist_suffix=fbt_options.DIST_SUFFIX,
                )
            )
            self.assertEqual(
                sorted(path.name for path in splash_dir.glob("frame_*.png")),
                ["frame_00.png", "frame_01.png", "frame_02.png", "frame_03.png"],
            )
            self.assertTrue(sync_update_splash(splash_dir, check=True))

    def test_dist_suffix_version_parser(self) -> None:
        self.assertEqual(version_from_dist_suffix("tmwhflpprarf089-031"), "089-031")
        self.assertIsNone(version_from_dist_suffix("abcdef12"))

    def test_fbt_autogenerates_tumoflip_update_splash(self) -> None:
        sconstruct = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")

        self.assertIn('UPDATE_SPLASH"] == "tumoflip_update"', sconstruct)
        self.assertIn("sync_update_splash.py", sconstruct)
        self.assertIn("Depends(selfupdate_dist, update_splash)", sconstruct)


if __name__ == "__main__":
    unittest.main()
