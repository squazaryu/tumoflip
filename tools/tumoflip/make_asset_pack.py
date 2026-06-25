#!/usr/bin/env python3

import argparse
import io
import re
import struct
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


PACK_NAME_RE = re.compile(r"^[A-Za-z0-9_-]+$")
ICON_WIDTH = 14
ICON_HEIGHT = 14
MAX_FRAME_SIZE = 256
ASSET_PACK_EXT_PATH = Path("apps_data/tumoflip/asset_packs")
MANIFEST_FILENAME = "manifest.txt"
MANIFEST_FILETYPE = "Tumoflip Asset Pack"
MANIFEST_VERSION = 1
MANIFEST_TARGET = "desktop-ok-menu"

DEFAULT_ICON_SOURCES = {
    "ModuleOne_14.bmx": REPO_ROOT / "assets/icons/MainMenu/ModuleOne_14/frame_01.png",
    "ARFTools_14.bmx": REPO_ROOT / "assets/icons/MainMenu/ARFTools_14/frame_0.png",
}


class AssetPackError(Exception):
    pass


def validate_pack_name(pack_name: str) -> None:
    if not PACK_NAME_RE.fullmatch(pack_name):
        raise AssetPackError(
            "pack name must contain only letters, digits, underscores, and hyphens"
        )


def ext_asset_pack_root(ext_root: Path) -> Path:
    return ext_root / ASSET_PACK_EXT_PATH


def png_to_bmx(source: Path) -> bytes:
    if not source.is_file():
        raise AssetPackError(f"icon source does not exist: {source}")

    try:
        from PIL import Image, ImageOps
    except ImportError as error:
        raise AssetPackError("Pillow is required to build asset packs") from error

    with Image.open(source) as image:
        width, height = image.size
        if width != ICON_WIDTH or height != ICON_HEIGHT:
            raise AssetPackError(
                f"{source} is {width}x{height}; expected {ICON_WIDTH}x{ICON_HEIGHT}"
            )

        with io.BytesIO() as output:
            bitmap = ImageOps.invert(image.convert("1"))
            bitmap.save(output, format="XBM")
            xbm_text = output.getvalue().decode().strip()

    data = xbm_text.replace("\n", "").replace(" ", "").split("=")[1][:-1]
    data_str = data[1:-1].replace(",", " ").replace("0x", "")
    frame = b"\x00" + bytes.fromhex(data_str)

    if len(frame) > MAX_FRAME_SIZE:
        raise AssetPackError(
            f"{source} encoded frame is {len(frame)} bytes; max is {MAX_FRAME_SIZE}"
        )

    return struct.pack("<II", width, height) + frame


def build_manifest(pack_name: str, icon_sources: dict[str, Path]) -> str:
    expected_icons = {"ModuleOne_14.bmx", "ARFTools_14.bmx"}
    actual_icons = set(icon_sources)
    if actual_icons != expected_icons:
        raise AssetPackError(
            f"icon set mismatch; expected {sorted(expected_icons)}, got {sorted(actual_icons)}"
        )

    return "\n".join(
        [
            f"Filetype: {MANIFEST_FILETYPE}",
            f"Version: {MANIFEST_VERSION}",
            f"Name: {pack_name}",
            f"Target: {MANIFEST_TARGET}",
            "ModuleOneIcon: ModuleOne_14.bmx",
            "ARFToolsIcon: ARFTools_14.bmx",
            "",
        ]
    )


def build_asset_pack(
    pack_name: str,
    output_root: Path,
    icon_sources: dict[str, Path],
    write_active: bool = True,
) -> list[Path]:
    validate_pack_name(pack_name)

    icons_root = output_root / pack_name / "Icons"
    icons_root.mkdir(parents=True, exist_ok=True)

    written: list[Path] = []
    manifest = output_root / pack_name / MANIFEST_FILENAME
    manifest.write_text(build_manifest(pack_name, icon_sources), encoding="utf-8")
    written.append(manifest)

    for filename, source in sorted(icon_sources.items()):
        target = icons_root / filename
        target.write_bytes(png_to_bmx(source))
        written.append(target)

    if write_active:
        active = output_root / "active.txt"
        active.write_text(f"{pack_name}\n", encoding="utf-8")
        written.append(active)

    return written


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a Tumoflip SD asset pack for custom Desktop OK-menu icons."
    )
    parser.add_argument("pack_name", help="Asset pack directory name")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=REPO_ROOT / "build/tumoflip-asset-packs",
        help="Directory representing /ext/apps_data/tumoflip/asset_packs",
    )
    parser.add_argument(
        "--ext-root",
        type=Path,
        help="Mounted SD root; writes under apps_data/tumoflip/asset_packs",
    )
    parser.add_argument(
        "--module-one",
        type=Path,
        default=DEFAULT_ICON_SOURCES["ModuleOne_14.bmx"],
        help="14x14 PNG source for ModuleOne_14.bmx",
    )
    parser.add_argument(
        "--arf-tools",
        type=Path,
        default=DEFAULT_ICON_SOURCES["ARFTools_14.bmx"],
        help="14x14 PNG source for ARFTools_14.bmx",
    )
    parser.add_argument(
        "--no-active",
        action="store_true",
        help="Do not write active.txt",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_root = ext_asset_pack_root(args.ext_root) if args.ext_root else args.output_root
    icon_sources = {
        "ModuleOne_14.bmx": args.module_one,
        "ARFTools_14.bmx": args.arf_tools,
    }

    try:
        written = build_asset_pack(
            args.pack_name,
            output_root,
            icon_sources,
            write_active=not args.no_active,
        )
    except AssetPackError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
