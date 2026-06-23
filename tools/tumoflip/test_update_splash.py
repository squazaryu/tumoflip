#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

import fbt_options

from tools.tumoflip.generate_update_splash import generate


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


if __name__ == "__main__":
    unittest.main()
