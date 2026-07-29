#!/usr/bin/env python3
"""Render the illustrated Tumoflip post-update slideshow at 128x64."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


REPO_ROOT = Path(__file__).resolve().parents[2]
DISPLAY_SIZE = (128, 64)
SOURCE_ART_DIR = REPO_ROOT / "assets/tumoflip/update_splash_art"
GRAVITY_FONT_DIR = REPO_ROOT / "assets/tumoflip/fonts/gravity"
GRAVITY_BOLD = GRAVITY_FONT_DIR / "GravityBold8.ttf"
GRAVITY_REGULAR = GRAVITY_FONT_DIR / "GravityRegular5.ttf"
NEXT_BUTTON = (88, 50, 125, 62)
READY_DEVICE_SCREEN = (66, 25, 85, 39)
NEXT_ARROW_PIXELS = (
    (2, -2),
    (3, -1),
    (0, 0),
    (1, 0),
    (2, 0),
    (3, 0),
    (4, 0),
    (3, 1),
    (2, 2),
)
FRAME_COUNT = 4
SOURCE_ART_PATTERN = "panel_{index:02d}_v2.png"
# The v2 panels deliberately use fewer, wider contours that survive the nearly
# 14x reduction. Keeping the normal cutoff preserves continuous strokes without
# merging nearby details into solid black areas.
BLACK_THRESHOLD = 176
# The toolkit scene contains a complete mascot/device composition with thinner
# contours than the other panels. A local cutoff keeps those contours intact
# without darkening the remaining three screens.
TOOLKIT_BLACK_THRESHOLD = 192
HEADER_GLYPH_HEIGHT = 7
HEADER_GLYPH_SPACING = 1
HEADER_SPACE_WIDTH = 3
HEADER_GLYPHS = {
    "A": (0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001),
    "D": (0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110),
    "E": (0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111),
    "F": (0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000),
    "G": (0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110),
    "H": (0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001),
    "I": (0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111),
    "K": (0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001),
    "L": (0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111),
    "M": (0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001),
    "N": (0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001),
    "O": (0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110),
    "P": (0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000),
    "R": (0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001),
    "S": (0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110),
    "T": (0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100),
    "U": (0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110),
    "X": (0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001),
    "Y": (0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100),
}


def load_gravity_font(path: Path, size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.truetype(path, size=size)
    except OSError:
        return ImageFont.load_default()


def gravity_bold(size: int) -> ImageFont.ImageFont:
    return load_gravity_font(GRAVITY_BOLD, size)


def gravity_regular(size: int) -> ImageFont.ImageFont:
    return load_gravity_font(GRAVITY_REGULAR, size)


def text_size(
    draw: ImageDraw.ImageDraw,
    text: str,
    font: ImageFont.ImageFont,
) -> tuple[int, int]:
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0], bbox[3] - bbox[1]


def header_text_width(text: str) -> int:
    glyph_width = sum(
        HEADER_SPACE_WIDTH if character == " " else 5 for character in text
    )
    return glyph_width + max(0, len(text) - 1) * HEADER_GLYPH_SPACING


def draw_header_text(
    draw: ImageDraw.ImageDraw,
    text: str,
    *,
    x: int,
    y: int,
) -> None:
    """Draw crisp uppercase text directly on the display pixel grid."""

    cursor_x = x
    for character in text:
        if character == " ":
            cursor_x += HEADER_SPACE_WIDTH + HEADER_GLYPH_SPACING
            continue
        try:
            rows = HEADER_GLYPHS[character]
        except KeyError as error:
            raise ValueError(f"Unsupported header glyph: {character}") from error
        for row_index, row_bits in enumerate(rows):
            for column in range(5):
                if row_bits & (1 << (4 - column)):
                    draw.point((cursor_x + column, y + row_index), fill=0)
        cursor_x += 5 + HEADER_GLYPH_SPACING


def draw_header_tab(
    draw: ImageDraw.ImageDraw,
    text: str,
    *,
    left: int,
    right: int,
) -> None:
    """Replace downscaled heading pixels with crisp display-native text."""

    top = 4
    bottom = 14
    # Preserve one white pixel row between the outer frame and the title tab.
    draw.rectangle((left - 1, 3, min(126, right + 1), bottom), fill=1)
    tab = (
        (left, top),
        (right, top),
        (right, 11),
        (right - 4, bottom),
        (left + 4, bottom),
        (left, 11),
    )
    draw.polygon(tab, fill=1, outline=0)
    text_width = header_text_width(text)
    available_width = right - left + 1
    if text_width > available_width - 8:
        raise ValueError(f"Header text does not fit: {text}")
    text_x = left + (available_width - text_width) // 2
    draw_header_text(draw, text, x=text_x, y=6)


def draw_button(
    draw: ImageDraw.ImageDraw,
    text: str,
    button: tuple[int, int, int, int] = NEXT_BUTTON,
) -> None:
    """Draw a self-clearing two-layer action button."""

    x1, y1, x2, y2 = button
    clear = (x1 - 2, y1 - 2, min(127, x2 + 2), min(63, y2 + 1))
    draw.rectangle(clear, fill=1)
    draw.rectangle(
        (x1 + 2, y1 + 2, min(127, x2 + 1), min(63, y2 + 1)),
        outline=0,
    )
    draw.rectangle(button, fill=1, outline=0)

    # NEXT needs a narrower 7 px face so its left glyph and right arrow keep a
    # real white gutter inside the 38 px button instead of touching its frame.
    font = gravity_bold(7 if text == "NEXT" else 8)
    arrow_space = 7 if text == "NEXT" else 0
    text_width, text_height = text_size(draw, text, font)
    content_width = text_width + arrow_space
    text_x = x1 + ((x2 - x1 + 1) - content_width) // 2
    text_y = y1 + ((y2 - y1 + 1) - text_height) // 2
    draw.text((text_x, text_y), text, font=font, fill=0)
    if text == "NEXT":
        arrow_x = text_x + text_width + 2
        arrow_center_y = y1 + (y2 - y1 + 1) // 2
        for offset_x, offset_y in NEXT_ARROW_PIXELS:
            draw.point((arrow_x + offset_x, arrow_center_y + offset_y), fill=0)


def render_source_art(
    source: Path,
    black_threshold: int = BLACK_THRESHOLD,
) -> Image.Image:
    """Downsample the accepted line art and pack it as a strict 1-bit frame."""

    with Image.open(source) as original:
        if original.width * 1 != original.height * 2:
            raise ValueError(f"{source} must use an exact 2:1 aspect ratio")
        reduced = original.convert("L").resize(DISPLAY_SIZE, Image.Resampling.LANCZOS)

    return reduced.point(
        lambda value: 255 if value >= black_threshold else 0,
        mode="1",
    )


def draw_protocol_labels(draw: ImageDraw.ImageDraw) -> None:
    """Restore legible labels lost when the dense toolkit art is reduced."""

    draw.rectangle((3, 40, 62, 49), fill=1)
    font = gravity_regular(5)
    labels = (("RF", 5), ("NFC", 17), ("IR", 34), ("BLE", 45))
    for label, x in labels:
        draw.text((x, 42), label, font=font, fill=0)


def draw_progress_strip(draw: ImageDraw.ImageDraw) -> None:
    """Keep the first panel's status strip readable instead of solid black."""

    draw.rectangle((3, 51, 68, 60), fill=1)
    draw.rectangle((4, 52, 67, 59), outline=0)
    for x in range(7, 64, 5):
        draw.rectangle((x, 54, x + 2, 57), fill=0)


def draw_bar_display(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    heights: tuple[int, ...],
) -> None:
    """Restore the white spectrum bars inside a reduced black device screen."""

    x1, y1, x2, y2 = box
    draw.rectangle(box, fill=0)
    draw.rectangle((x1 + 1, y1 + 1, x2 - 1, y2 - 1), outline=1)
    baseline = y2 - 2
    available_width = x2 - x1 - 4
    step = max(2, available_width // len(heights))
    for index, height in enumerate(heights):
        x = x1 + 3 + index * step
        draw.line((x, baseline, x, max(y1 + 2, baseline - height)), fill=1)


def draw_signal_overview(draw: ImageDraw.ImageDraw) -> None:
    """Replace four lossy mini-icons with one legible signal display."""

    draw.rectangle((3, 13, 61, 39), fill=1)
    draw.rectangle((5, 14, 59, 37), outline=0)
    draw.rectangle((8, 17, 56, 34), outline=0)
    waveform = (
        (10, 27),
        (15, 22),
        (20, 30),
        (25, 19),
        (30, 29),
        (35, 23),
        (40, 30),
        (45, 20),
        (51, 28),
        (54, 25),
    )
    draw.line(waveform, fill=0, width=1)


def polish_frame(image: Image.Image, index: int) -> Image.Image:
    """Apply only resolution-dependent typography and action controls."""

    draw = ImageDraw.Draw(image)
    if index == 0:
        draw_header_tab(draw, "TUMOFLIP UPDATED", left=4, right=122)
        draw_progress_strip(draw)
    elif index == 1:
        draw_header_tab(draw, "SIGNAL TOOLKIT", left=5, right=110)
        draw_signal_overview(draw)
        draw_protocol_labels(draw)
        draw_bar_display(draw, (64, 35, 80, 46), (2, 5, 3, 7, 4, 6))
    elif index == 2:
        draw_header_tab(draw, "EXPLORE THE AIR", left=4, right=116)
    elif index == 3:
        draw_header_tab(draw, "ALL SYSTEMS GO", left=4, right=107)
        draw_bar_display(draw, READY_DEVICE_SCREEN, (2, 5, 3, 7, 4, 6))

    draw_button(draw, "OK" if index == FRAME_COUNT - 1 else "NEXT")
    return image


def generate_slideshow(
    title: str,
    version: str,
    output_dir: Path,
    static_frames_dir: Path | None = None,
) -> list[Path]:
    """Generate channel-neutral artwork; version metadata stays in About."""

    del title, version
    source_dir = static_frames_dir or SOURCE_ART_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    frames: list[Path] = []
    for index in range(FRAME_COUNT):
        source = source_dir / SOURCE_ART_PATTERN.format(index=index)
        if not source.is_file():
            raise FileNotFoundError(f"Missing welcome art source: {source}")
        output = output_dir / f"frame_{index:02d}.png"
        threshold = {
            1: TOOLKIT_BLACK_THRESHOLD,
        }.get(index, BLACK_THRESHOLD)
        polish_frame(render_source_art(source, threshold), index).save(output)
        frames.append(output)

    return frames


def generate(title: str, version: str, output: Path) -> None:
    del title, version
    output.parent.mkdir(parents=True, exist_ok=True)
    source = SOURCE_ART_DIR / SOURCE_ART_PATTERN.format(index=0)
    polish_frame(render_source_art(source), 0).save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title", default="T-FLPPR-FW")
    parser.add_argument("--version", default="001")
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
