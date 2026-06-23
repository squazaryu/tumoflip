#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

import fbt_options

from tools.tumoflip.generate_update_splash import generate
from tools.tumoflip.sync_update_splash import sync_update_splash, version_from_dist_suffix


REPO_ROOT = Path(__file__).resolve().parents[2]
SPLASH_DIR = REPO_ROOT / "assets/slideshow/tumoflip_update"


class UpdateSplashTest(unittest.TestCase):
    def test_update_splash_matches_current_firmware_version(self) -> None:
        prefix = "tmwhflpprarf"
        self.assertTrue(fbt_options.DIST_SUFFIX.startswith(prefix))
        version = fbt_options.DIST_SUFFIX.removeprefix(prefix)

        with tempfile.TemporaryDirectory() as directory:
            expected = Path(directory) / "frame_00.png"
            generate("TMWHFLPPRARF", version, expected)
            actual = SPLASH_DIR / "frame_00.png"

            self.assertEqual(actual.read_bytes(), expected.read_bytes())

    def test_update_splash_is_a_single_page_screen(self) -> None:
        frames = sorted(path.name for path in SPLASH_DIR.glob("frame_*.png"))
        self.assertEqual(frames, ["frame_00.png"])

    def test_sync_update_splash_removes_stale_frames(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            splash_dir = Path(directory)
            (splash_dir / "frame_01.png").write_bytes(b"stale")

            self.assertTrue(
                sync_update_splash(
                    splash_dir,
                    dist_suffix=fbt_options.DIST_SUFFIX,
                )
            )
            self.assertEqual(
                sorted(path.name for path in splash_dir.glob("frame_*.png")),
                ["frame_00.png"],
            )
            self.assertTrue(sync_update_splash(splash_dir, check=True))

    def test_dist_suffix_version_parser(self) -> None:
        self.assertEqual(version_from_dist_suffix("tmwhflpprarf089-029"), "089-029")
        self.assertIsNone(version_from_dist_suffix("abcdef12"))

    def test_fbt_autogenerates_tumoflip_update_splash(self) -> None:
        sconstruct = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")

        self.assertIn('UPDATE_SPLASH"] == "tumoflip_update"', sconstruct)
        self.assertIn("sync_update_splash.py", sconstruct)
        self.assertIn("Depends(selfupdate_dist, update_splash)", sconstruct)


if __name__ == "__main__":
    unittest.main()
