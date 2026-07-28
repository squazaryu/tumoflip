#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


REPO_ROOT = Path(__file__).resolve().parents[2]
DISPLAY_SIZE = (128, 64)
FIRST_FRAME_SAFE_TOP = 18
# Keep the stable identifier clear of the updater button's reserved gap.
# Gravity Bold 16 extends below its text origin, so y=32 reaches row 46.
FIRST_FRAME_VERSION_Y = 30
DEV_FIRST_FRAME_VERSION_Y = 38
GRAVITY_FONT_DIR = REPO_ROOT / "assets/tumoflip/fonts/gravity"
GRAVITY_BOLD = GRAVITY_FONT_DIR / "GravityBold8.ttf"
GRAVITY_REGULAR = GRAVITY_FONT_DIR / "GravityRegular5.ttf"


SMALL_GLYPHS = {
    "E": (
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#####",
    ),
    "N": (
        "#...#",
        "##..#",
        "#.#.#",
        "#..##",
        "#...#",
        "#...#",
        "#...#",
    ),
    "O": (
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ),
    "K": (
        "#...#",
        "#..#.",
        "#.#..",
        "##...",
        "#.#..",
        "#..#.",
        "#...#",
    ),
    "T": (
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
    ),
    "X": (
        "#...#",
        ".#.#.",
        ".#.#.",
        "..#..",
        ".#.#.",
        ".#.#.",
        "#...#",
    ),
}

BIG_DIGITS = {
    "0": (
        "..####..",
        "..####..",
        "##....##",
        "##....##",
        "##..####",
        "##..####",
        "####..##",
        "####..##",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
    ),
    "1": (
        "..##..",
        "..##..",
        "####..",
        "####..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "######",
        "######",
    ),
    "2": (
        "..####..",
        "..####..",
        "##....##",
        "##....##",
        "......##",
        "......##",
        "..####..",
        "..####..",
        "##......",
        "##......",
        "########",
        "########",
    ),
    "3": (
        "######..",
        "######..",
        "......##",
        "......##",
        "..####..",
        "..####..",
        "......##",
        "......##",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
    ),
    "4": (
        "##....##",
        "##....##",
        "##....##",
        "##....##",
        "########",
        "########",
        "......##",
        "......##",
        "......##",
        "......##",
        "......##",
        "......##",
    ),
    "5": (
        "########",
        "########",
        "##......",
        "##......",
        "######..",
        "######..",
        "......##",
        "......##",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
    ),
    "6": (
        "..####..",
        "..####..",
        "##......",
        "##......",
        "######..",
        "######..",
        "##....##",
        "##....##",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
    ),
    "7": (
        "########",
        "########",
        "......##",
        "......##",
        "....##..",
        "....##..",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
    ),
    "8": (
        "..####..",
        "..####..",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
        "##....##",
        "##....##",
        "##....##",
        "##....##",
        "..####..",
        "..####..",
    ),
    "9": (
        "..####..",
        "..####..",
        "##....##",
        "##....##",
        "##....##",
        "##....##",
        "..######",
        "..######",
        "......##",
        "......##",
        "######..",
        "######..",
    ),
    "-": (
        "......",
        "......",
        "......",
        "......",
        "......",
        "......",
        "######",
        "######",
        "......",
        "......",
        "......",
        "......",
    ),
}


def draw_pixel_text(
    draw: ImageDraw.ImageDraw,
    glyphs: dict[str, tuple[str, ...]],
    text: str,
    x: int,
    y: int,
    spacing: int = 1,
) -> None:
    cursor = x
    for char in text:
        glyph = glyphs[char]
        for row_index, row in enumerate(glyph):
            for col_index, pixel in enumerate(row):
                if pixel == "#":
                    draw.point((cursor + col_index, y + row_index), fill=0)
        cursor += len(glyph[0]) + spacing


def pixel_text_width(
    glyphs: dict[str, tuple[str, ...]], text: str, spacing: int = 1
) -> int:
    return sum(len(glyphs[char][0]) for char in text) + max(0, len(text) - 1) * spacing


def draw_big_text(draw: ImageDraw.ImageDraw, text: str, x: int, y: int) -> None:
    draw_pixel_text(draw, BIG_DIGITS, text, x, y, spacing=2)


def big_text_width(text: str) -> int:
    return pixel_text_width(BIG_DIGITS, text, spacing=2)


def load_gravity_font(path: Path, size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.truetype(path, size=size)
    except OSError:
        return ImageFont.load_default()


def gravity_bold(size: int) -> ImageFont.ImageFont:
    return load_gravity_font(GRAVITY_BOLD, size)


def gravity_regular(size: int) -> ImageFont.ImageFont:
    return load_gravity_font(GRAVITY_REGULAR, size)


def fit_gravity_bold(
    text: str,
    max_width: int,
    preferred_size: int = 16,
) -> ImageFont.ImageFont:
    for size in range(preferred_size, 7, -1):
        font = gravity_bold(size)
        bbox = font.getbbox(text)
        if bbox[2] - bbox[0] <= max_width:
            return font
    return gravity_bold(8)


def draw_next_button(draw: ImageDraw.ImageDraw) -> None:
    draw_button(draw, "NEXT", (88, 50, 125, 62))


def draw_button(
    draw: ImageDraw.ImageDraw, text: str, button: tuple[int, int, int, int]
) -> None:
    draw.rectangle(button, outline=0)

    font = gravity_bold(8)
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    text_x = button[0] + ((button[2] - button[0] + 1) - text_width) // 2
    text_y = button[1] + ((button[3] - button[1] + 1) - text_height) // 2
    draw.text((text_x, text_y), text, font=font, fill=0)


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    text: str,
    y: int,
    font: ImageFont.ImageFont,
    extra_word_spacing: int = 0,
) -> None:
    words = text.split(" ")
    if len(words) == 1:
        bbox = draw.textbbox((0, 0), text, font=font)
        width = bbox[2] - bbox[0]
        draw.text(((DISPLAY_SIZE[0] - width) // 2, y), text, font=font, fill=0)
        return

    space_bbox = draw.textbbox((0, 0), " ", font=font)
    space_width = max(3, space_bbox[2] - space_bbox[0]) + extra_word_spacing
    word_widths = []
    for word in words:
        bbox = draw.textbbox((0, 0), word, font=font)
        word_widths.append(bbox[2] - bbox[0])

    width = sum(word_widths) + space_width * (len(words) - 1)
    x = (DISPLAY_SIZE[0] - width) // 2
    for word, word_width in zip(words, word_widths):
        draw.text((x, y), word, font=font, fill=0)
        x += word_width + space_width


def generate_slideshow(
    title: str,
    version: str,
    output_dir: Path,
    static_frames_dir: Path | None = None,
) -> list[Path]:
    frames = [
        output_dir / "frame_00.png",
        output_dir / "frame_01.png",
        output_dir / "frame_02.png",
        output_dir / "frame_03.png",
    ]

    output_dir.mkdir(parents=True, exist_ok=True)
    generate(title, version, frames[0])
    if title == "T-DEV":
        frame_01_lines = (
            ("TUMOFLIP FIRMWARE", gravity_bold(8), 22),
            ("TUMOFLIP DEV", gravity_bold(8), 36),
        )
        frame_02_lines = (
            ("DEV BUILD", gravity_bold(8), 22),
            ("MAY BE UNSTABLE", gravity_bold(8), 36),
        )
    else:
        frame_01_lines = (
            ("TUMOFLIP FIRMWARE", gravity_bold(8), 22),
            ("STABLE RELEASE", gravity_bold(8), 36),
        )
        frame_02_lines = (
            ("WELCOME TO", gravity_bold(8), 22),
            ("TUMOFLIP", gravity_bold(8), 36),
        )
    generate_message_frame(
        frame_01_lines,
        frames[1],
    )
    generate_message_frame(
        frame_02_lines,
        frames[2],
    )
    generate_message_frame(
        (
            ("ISSUES", gravity_bold(16), 16),
            ("GH: SQUAZARYU/TUMOFLIP", gravity_regular(5), 39),
        ),
        frames[3],
        button="OK",
    )

    return frames


def generate(title: str, version: str, output: Path) -> None:
    unsupported = sorted(set(version) - set(BIG_DIGITS))
    if unsupported:
        raise ValueError(f"Unsupported version characters: {''.join(unsupported)}")

    image = Image.new("1", DISPLAY_SIZE, 1)
    draw = ImageDraw.Draw(image)
    title_font = fit_gravity_bold(title, 116)
    version_font = gravity_bold(16)

    draw_centered_text(draw, title, FIRST_FRAME_SAFE_TOP, title_font)

    if title == "T-DEV":
        # Keep the dev identifier clear of both the title and the updater button.
        # Even short identifiers must use the compact line: the full-width
        # 16/12 px variants visually collide with the button in a 64 px frame.
        version_font = gravity_bold(8)
        version_y = DEV_FIRST_FRAME_VERSION_Y
    else:
        version_font = fit_gravity_bold(version, 116)
        version_y = FIRST_FRAME_VERSION_Y
    draw_centered_text(draw, version, version_y, version_font)

    draw_next_button(draw)

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def generate_message_frame(
    lines: tuple[tuple[str, ImageFont.ImageFont, int], ...],
    output: Path,
    button: str = "NEXT",
) -> None:
    image = Image.new("1", DISPLAY_SIZE, 1)
    draw = ImageDraw.Draw(image)

    for text, font, y in lines:
        draw_centered_text(draw, text, y, font)
    draw_button(draw, button, (88, 50, 125, 62))

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", default="T-FLPPR-FW")
    parser.add_argument("--version", default="089-031")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/slideshow/tumoflip_update/frame_00.png"),
    )
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.output_dir:
        generate_slideshow(args.title, args.version, args.output_dir)
    else:
        generate(args.title, args.version, args.output)


if __name__ == "__main__":
    main()
