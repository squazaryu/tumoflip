#!/usr/bin/env python3

from pathlib import Path
import json
import tempfile
import unittest

import fbt_options
from PIL import Image, ImageDraw
from PIL.PngImagePlugin import PngInfo

from tools.tumoflip.generate_update_splash import (
    DISPLAY_SIZE,
    NEXT_ARROW_PIXELS,
    SOURCE_ART_DIR,
    SOURCE_ART_PATTERN,
    draw_button,
    fit_gravity_bold,
    generate_slideshow,
)
from tools.tumoflip.sync_update_splash import (
    MANIFEST_NAME,
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
        self.assertTrue(
            sync_update_splash(
                SPLASH_DIR,
                dist_suffix=fbt_options.DIST_SUFFIX,
                check=True,
            )
        )

        manifest = json.loads(
            (SPLASH_DIR / MANIFEST_NAME).read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["title"], title)
        self.assertEqual(manifest["version"], version)

    def test_update_splash_has_expected_pages(self) -> None:
        frames = sorted(path.name for path in SPLASH_DIR.glob("frame_*.png"))
        self.assertEqual(
            frames,
            ["frame_00.png", "frame_01.png", "frame_02.png", "frame_03.png"],
        )

    def test_pages_are_dense_one_bit_illustrations(self) -> None:
        for frame_path in sorted(SPLASH_DIR.glob("frame_*.png")):
            with Image.open(frame_path) as frame:
                self.assertEqual(frame.mode, "1", frame_path.name)
                self.assertEqual(frame.size, DISPLAY_SIZE, frame_path.name)
                black_ratio = sum(pixel == 0 for pixel in frame.getdata()) / (
                    DISPLAY_SIZE[0] * DISPLAY_SIZE[1]
                )
                self.assertGreater(black_ratio, 0.18, frame_path.name)
                self.assertLess(black_ratio, 0.34, frame_path.name)

    def test_art_is_channel_and_version_neutral(self) -> None:
        with tempfile.TemporaryDirectory() as stable, tempfile.TemporaryDirectory() as dev:
            stable_frames = generate_slideshow(
                "T-FLPPR-FW",
                "001",
                Path(stable),
            )
            dev_frames = generate_slideshow(
                "T-DEV",
                "001-002",
                Path(dev),
            )
            for stable_frame, dev_frame in zip(stable_frames, dev_frames):
                self.assertPngPixelsEqual(stable_frame, dev_frame)

    def test_action_buttons_clear_their_reserved_area(self) -> None:
        crop_box = (86, 48, 128, 64)
        for index, label in enumerate(("NEXT", "NEXT", "NEXT", "OK")):
            expected = Image.new("1", DISPLAY_SIZE, 1)
            draw_button(ImageDraw.Draw(expected), label)
            with Image.open(SPLASH_DIR / f"frame_{index:02d}.png") as frame:
                self.assertEqual(
                    frame.convert("1").crop(crop_box).tobytes(),
                    expected.crop(crop_box).tobytes(),
                )

    def test_next_buttons_keep_white_gutters_inside_the_frame(self) -> None:
        for index in range(3):
            with Image.open(SPLASH_DIR / f"frame_{index:02d}.png") as frame:
                pixels = frame.convert("1")
                for y in range(51, 62):
                    self.assertEqual(pixels.getpixel((89, y)), 255, (index, 89, y))
                    self.assertEqual(pixels.getpixel((124, y)), 255, (index, 124, y))

    def test_next_arrow_is_a_symmetric_pixel_arrow(self) -> None:
        expected = {(118 + x, 56 + y) for x, y in NEXT_ARROW_PIXELS}
        self.assertEqual(
            expected,
            {
                (120, 54),
                (121, 55),
                (118, 56),
                (119, 56),
                (120, 56),
                (121, 56),
                (122, 56),
                (121, 57),
                (120, 58),
            },
        )
        for index in range(3):
            with Image.open(SPLASH_DIR / f"frame_{index:02d}.png") as frame:
                pixels = frame.convert("1")
                actual = {
                    (x, y)
                    for y in range(54, 59)
                    for x in range(118, 123)
                    if pixels.getpixel((x, y)) == 0
                }
                self.assertEqual(actual, expected, index)

    def test_generated_pages_use_approved_illustrated_sources(self) -> None:
        generator = (REPO_ROOT / "tools/tumoflip/generate_update_splash.py").read_text(
            encoding="utf-8"
        )

        for phrase in (
            "SOURCE_ART_DIR",
            "render_source_art",
            "TUMOFLIP UPDATED",
            "SIGNAL TOOLKIT",
            "EXPLORE THE AIR",
            "draw_ready_wordmark",
            '"READY"',
            '"TUMOFLIP"',
            "GravityBold8.ttf",
            "GravityRegular5.ttf",
        ):
            self.assertIn(phrase, generator)

        for index in range(4):
            source = SOURCE_ART_DIR / SOURCE_ART_PATTERN.format(index=index)
            self.assertTrue(source.is_file(), source.name)
            with Image.open(source) as image:
                self.assertEqual(image.mode, "1", source.name)
                self.assertEqual(image.width, image.height * 2, source.name)

    def test_gravity_font_assets_are_vendored(self) -> None:
        for name in ("GravityBold8.ttf", "GravityRegular5.ttf", "README.md"):
            self.assertTrue((GRAVITY_FONT_DIR / name).exists(), name)

    def test_welcome_wordmarks_fit_the_display(self) -> None:
        for text, max_width, preferred_size in (
            ("TUMOFLIP UPDATED", 100, 8),
            ("SIGNAL TOOLKIT", 87, 8),
            ("EXPLORE THE AIR", 88, 8),
            ("TUMOFLIP", 119, 16),
        ):
            font = fit_gravity_bold(text, max_width, preferred_size)
            bbox = font.getbbox(text)
            self.assertLessEqual(bbox[2] - bbox[0], max_width)

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

    def test_sync_check_rejects_changed_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            splash_dir = Path(directory)
            sync_update_splash(splash_dir, dist_suffix=fbt_options.DIST_SUFFIX)
            frame = splash_dir / "frame_00.png"

            with Image.open(frame) as image:
                changed = image.copy()
            changed.putpixel((0, 0), 0)
            changed.save(frame)

            self.assertFalse(
                sync_update_splash(
                    splash_dir,
                    dist_suffix=fbt_options.DIST_SUFFIX,
                    check=True,
                )
            )

    def test_dist_suffix_version_parser(self) -> None:
        self.assertEqual(version_from_dist_suffix("t-flppr-fw-001"), "001")
        self.assertEqual(version_from_dist_suffix("t-flppr-fw-089-037"), "089-037")
        self.assertEqual(version_from_dist_suffix("tmwhflpprarf089-031"), "089-031")
        self.assertEqual(version_from_dist_suffix("t-dev-089-035-001"), "089-035-001")
        self.assertEqual(version_from_dist_suffix("t-dev-002-001"), "002-001")
        self.assertIsNone(version_from_dist_suffix("abcdef12"))

    def test_dev_splash_uses_dev_title(self) -> None:
        self.assertEqual(
            current_splash_metadata("t-dev-089-035-001"),
            ("T-DEV", "089-035-001"),
        )
        self.assertEqual(
            current_splash_metadata("t-flppr-fw-001"),
            ("T-FLPPR-FW", "001"),
        )
        self.assertEqual(
            current_splash_metadata("t-flppr-fw-089-037"),
            ("T-FLPPR-FW", "089-037"),
        )
        self.assertEqual(
            current_splash_metadata("tmwhflpprarf089-031"),
            ("T-FLPPR-FW", "089-031"),
        )

    def test_fbt_checks_tumoflip_update_splash_without_rewriting_assets(self) -> None:
        sconstruct = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")

        self.assertIn('UPDATE_SPLASH"] == "tumoflip_update"', sconstruct)
        self.assertIn("sync_update_splash.py", sconstruct)
        self.assertIn("tumoflip-update-splash-${DIST_SUFFIX}.stamp", sconstruct)
        self.assertIn('"--check"', sconstruct)
        self.assertIn("Depends(selfupdate_dist, update_splash)", sconstruct)


if __name__ == "__main__":
    unittest.main()
