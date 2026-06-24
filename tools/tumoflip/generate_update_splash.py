#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


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


def pixel_text_width(glyphs: dict[str, tuple[str, ...]], text: str, spacing: int = 1) -> int:
    return sum(len(glyphs[char][0]) for char in text) + max(0, len(text) - 1) * spacing


def draw_big_text(draw: ImageDraw.ImageDraw, text: str, x: int, y: int) -> None:
    draw_pixel_text(draw, BIG_DIGITS, text, x, y, spacing=2)


def big_text_width(text: str) -> int:
    return pixel_text_width(BIG_DIGITS, text, spacing=2)


def draw_next_button(draw: ImageDraw.ImageDraw) -> None:
    draw_button(draw, "NEXT", (94, 45, 125, 57))


def draw_button(draw: ImageDraw.ImageDraw, text: str, button: tuple[int, int, int, int]) -> None:
    draw.rectangle(button, outline=0)

    text_width = pixel_text_width(SMALL_GLYPHS, text)
    text_x = button[0] + ((button[2] - button[0] + 1) - text_width) // 2
    draw_pixel_text(draw, SMALL_GLYPHS, text, text_x, 48)


def draw_text_page(
    title: str,
    lines: list[str],
    output: Path,
    button_text: str = "NEXT",
) -> None:
    image = Image.new("1", (128, 64), 1)
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()

    title_bbox = draw.textbbox((0, 0), title, font=font)
    title_width = title_bbox[2] - title_bbox[0]
    draw.text(((128 - title_width) // 2, 10), title, font=font, fill=0)

    y = 27
    for line in lines:
        line_bbox = draw.textbbox((0, 0), line, font=font)
        line_width = line_bbox[2] - line_bbox[0]
        draw.text(((128 - line_width) // 2, y), line, font=font, fill=0)
        y += 11

    if button_text == "OK":
        draw_button(draw, "OK", (105, 45, 125, 57))
    else:
        draw_next_button(draw)

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def generate_slideshow(title: str, version: str, output_dir: Path) -> list[Path]:
    base_version = version.split("-", maxsplit=1)[0]
    frames = [
        output_dir / "frame_00.png",
        output_dir / "frame_01.png",
        output_dir / "frame_02.png",
        output_dir / "frame_03.png",
    ]

    generate(title, version, frames[0])
    draw_text_page("UNLEASHED " + base_version, ["TUMOFLIP FORK"], frames[1])
    draw_text_page("EXPERIMENTAL", ["MAY BE UNSTABLE"], frames[2])
    draw_text_page("ISSUES", ["SQUAZARYU", "TUMOFLIP"], frames[3], button_text="OK")
    return frames


def generate(title: str, version: str, output: Path) -> None:
    unsupported = sorted(set(version) - set(BIG_DIGITS))
    if unsupported:
        raise ValueError(f"Unsupported version characters: {''.join(unsupported)}")

    image = Image.new("1", (128, 64), 1)
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()

    title_bbox = draw.textbbox((0, 0), title, font=font)
    title_width = title_bbox[2] - title_bbox[0]
    draw.text(((128 - title_width) // 2, 21), title, font=font, fill=0)

    version_width = big_text_width(version)
    draw_big_text(draw, version, (128 - version_width) // 2, 32)

    draw_next_button(draw)

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", default="TMWHFLPPRARF")
    parser.add_argument("--version", default="089-030")
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
