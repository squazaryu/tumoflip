#!/usr/bin/env python3

from pathlib import Path
import tempfile
import unittest

import fbt_options
from PIL import Image
from PIL.PngImagePlugin import PngInfo

from tools.tumoflip.generate_update_splash import fit_gravity_bold, generate_slideshow
from tools.tumoflip.sync_update_splash import (
    current_splash_metadata,
    sync_update_splash,
    version_from_dist_suffix,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
SPLASH_DIR = REPO_ROOT / "assets/slideshow/tumoflip_update"
GRAVITY_FONT_DIR = REPO_ROOT / "assets/tumoflip/fonts/gravity"


class UpdateSplashTest(unittest.TestCase):
    def assertPngPixelsEqual(self, actual_path: Path, expected_path: Path) -> None:
        with Image.open(actual_path) as actual, Image.open(expected_path) as expected:
            self.assertEqual(actual.mode, expected.mode)
            self.assertEqual(actual.size, expected.size)
            self.assertEqual(actual.tobytes(), expected.tobytes())

    def test_update_splash_matches_current_firmware_version(self) -> None:
        title, version = current_splash_metadata(fbt_options.DIST_SUFFIX)

        with tempfile.TemporaryDirectory() as directory:
            expected_dir = Path(directory)
            expected_frames = generate_slideshow(title, version, expected_dir)

            for expected in expected_frames:
                actual = SPLASH_DIR / expected.name
                self.assertPngPixelsEqual(actual, expected)

    def test_update_splash_has_expected_pages(self) -> None:
        frames = sorted(path.name for path in SPLASH_DIR.glob("frame_*.png"))
        self.assertEqual(
            frames,
            ["frame_00.png", "frame_01.png", "frame_02.png", "frame_03.png"],
        )

    def test_first_page_keeps_top_bar_clear(self) -> None:
        with Image.open(SPLASH_DIR / "frame_00.png") as frame:
            top_bar_area = frame.convert("1").crop((0, 0, 128, 18))
            self.assertEqual(top_bar_area.getextrema(), (255, 255))

    def test_generated_pages_use_friendly_post_install_copy(self) -> None:
        generator = (REPO_ROOT / "tools/tumoflip/generate_update_splash.py").read_text(
            encoding="utf-8"
        )

        for phrase in (
            "UNLEASHED {base_version}",
            "TUMOWUH FIRMWARE",
            "TUMOFLIP DEV",
            "CUSTOM BUILD",
            "USE WITH CARE",
            "DEV BUILD",
            "MAY BE UNSTABLE",
            "ISSUES",
            "GH: SQUAZARYU/TUMOFLIP",
            "GravityBold8.ttf",
            "GravityRegular5.ttf",
        ):
            self.assertIn(phrase, generator)

    def test_gravity_font_assets_are_vendored(self) -> None:
        for name in ("GravityBold8.ttf", "GravityRegular5.ttf", "README.md"):
            self.assertTrue((GRAVITY_FONT_DIR / name).exists(), name)

    def test_stable_brand_fits_the_display(self) -> None:
        font = fit_gravity_bold("T-FLPPR-FW", 116)
        bbox = font.getbbox("T-FLPPR-FW")
        self.assertLessEqual(bbox[2] - bbox[0], 116)
        self.assertGreaterEqual(font.size, 12)

        with tempfile.TemporaryDirectory() as directory:
            frames = generate_slideshow(
                "T-FLPPR-FW",
                "089-037",
                Path(directory),
            )
            with Image.open(frames[0]) as frame:
                top_bar_area = frame.convert("1").crop((0, 0, 128, 18))
                self.assertEqual(top_bar_area.getextrema(), (255, 255))

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

    def test_sync_keeps_pixel_identical_png_encoding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            splash_dir = Path(directory)
            sync_update_splash(splash_dir, dist_suffix=fbt_options.DIST_SUFFIX)
            frame = splash_dir / "frame_00.png"

            metadata = PngInfo()
            metadata.add_text("generator", "alternate-pillow-encoding")
            with Image.open(frame) as image:
                image.save(frame, pnginfo=metadata)
            encoded = frame.read_bytes()

            self.assertTrue(
                sync_update_splash(
                    splash_dir,
                    dist_suffix=fbt_options.DIST_SUFFIX,
                    check=True,
                )
            )
            sync_update_splash(splash_dir, dist_suffix=fbt_options.DIST_SUFFIX)
            self.assertEqual(frame.read_bytes(), encoded)

    def test_dist_suffix_version_parser(self) -> None:
        self.assertEqual(version_from_dist_suffix("t-flppr-fw-089-037"), "089-037")
        self.assertEqual(version_from_dist_suffix("tmwhflpprarf089-031"), "089-031")
        self.assertEqual(version_from_dist_suffix("t-dev-089-035-001"), "089-035-001")
        self.assertIsNone(version_from_dist_suffix("abcdef12"))

    def test_dev_splash_uses_dev_title(self) -> None:
        self.assertEqual(
            current_splash_metadata("t-dev-089-035-001"),
            ("T-DEV", "089-035-001"),
        )
        self.assertEqual(
            current_splash_metadata("t-flppr-fw-089-037"),
            ("T-FLPPR-FW", "089-037"),
        )
        self.assertEqual(
            current_splash_metadata("tmwhflpprarf089-031"),
            ("T-FLPPR-FW", "089-031"),
        )

    def test_fbt_autogenerates_tumoflip_update_splash(self) -> None:
        sconstruct = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")

        self.assertIn('UPDATE_SPLASH"] == "tumoflip_update"', sconstruct)
        self.assertIn("sync_update_splash.py", sconstruct)
        self.assertIn("Depends(selfupdate_dist, update_splash)", sconstruct)


if __name__ == "__main__":
    unittest.main()
