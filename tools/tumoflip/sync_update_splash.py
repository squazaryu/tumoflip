#!/usr/bin/env python3
"""Synchronize the tumoflip post-update splash with the firmware version."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import sys
from pathlib import Path

from PIL import Image, UnidentifiedImageError

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import fbt_options

try:
    from tools.tumoflip.generate_update_splash import generate_slideshow
except ModuleNotFoundError:
    from generate_update_splash import generate_slideshow


DEFAULT_TITLE = "T-FLPPR-FW"
DEV_TITLE = "T-DEV"
DEFAULT_PREFIX = "t-flppr-fw-"
LEGACY_STABLE_PREFIX = "tmwhflpprarf"
DEV_SUFFIX_RE = re.compile(r"^t-dev-(?P<version>\d{3}-\d{3}-\d{3})$")
DEFAULT_OUTPUT_DIR = Path("assets/slideshow/tumoflip_update")
MANIFEST_NAME = "manifest.json"
MANIFEST_SCHEMA = 1
EXPECTED_FRAME_NAMES = tuple(f"frame_{index:02}.png" for index in range(4))


def png_pixels_equal(path: Path, expected: bytes) -> bool:
    try:
        with (
            Image.open(path) as actual,
            Image.open(io.BytesIO(expected)) as reference,
        ):
            return (
                actual.mode == reference.mode
                and actual.size == reference.size
                and actual.tobytes() == reference.tobytes()
            )
    except (OSError, UnidentifiedImageError):
        return False


def png_pixel_metadata(path: Path) -> dict[str, object] | None:
    try:
        with Image.open(path) as image:
            pixels = image.tobytes()
            return {
                "mode": image.mode,
                "size": list(image.size),
                "pixels_sha256": hashlib.sha256(pixels).hexdigest(),
            }
    except (OSError, UnidentifiedImageError):
        return None


def splash_manifest(
    output_dir: Path,
    title: str,
    version: str,
) -> dict[str, object]:
    frames: dict[str, object] = {}
    for name in EXPECTED_FRAME_NAMES:
        metadata = png_pixel_metadata(output_dir / name)
        if metadata is None:
            raise ValueError(f"cannot read generated update splash frame: {name}")
        frames[name] = metadata
    return {
        "schema": MANIFEST_SCHEMA,
        "title": title,
        "version": version,
        "frames": frames,
    }


def manifest_matches(
    output_dir: Path,
    title: str,
    version: str,
) -> bool:
    manifest_path = output_dir / MANIFEST_NAME
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False

    if (
        manifest.get("schema") != MANIFEST_SCHEMA
        or manifest.get("title") != title
        or manifest.get("version") != version
    ):
        return False

    frames = manifest.get("frames")
    if not isinstance(frames, dict) or set(frames) != set(EXPECTED_FRAME_NAMES):
        return False

    actual_names = {path.name for path in output_dir.glob("frame_*.png")}
    if actual_names != set(EXPECTED_FRAME_NAMES):
        return False

    return all(
        png_pixel_metadata(output_dir / name) == frames[name]
        for name in EXPECTED_FRAME_NAMES
    )


def version_from_dist_suffix(
    dist_suffix: str, prefix: str = DEFAULT_PREFIX
) -> str | None:
    stable_prefixes = (prefix,)
    if prefix == DEFAULT_PREFIX:
        stable_prefixes += (LEGACY_STABLE_PREFIX,)
    for stable_prefix in stable_prefixes:
        if dist_suffix.startswith(stable_prefix):
            version = dist_suffix.removeprefix(stable_prefix)
            if re.fullmatch(r"\d{3}-\d{3}", version):
                return version

    dev_match = DEV_SUFFIX_RE.match(dist_suffix)
    if dev_match:
        return dev_match.group("version")

    return None


def current_version(
    dist_suffix: str | None = None, prefix: str = DEFAULT_PREFIX
) -> str:
    _, version = current_splash_metadata(dist_suffix, prefix=prefix)
    return version


def current_splash_metadata(
    dist_suffix: str | None = None,
    prefix: str = DEFAULT_PREFIX,
    title: str = DEFAULT_TITLE,
) -> tuple[str, str]:
    candidates = []
    if dist_suffix:
        candidates.append(dist_suffix)
    env_suffix = os.environ.get("DIST_SUFFIX")
    if env_suffix:
        candidates.append(env_suffix)
    candidates.append(fbt_options.DIST_SUFFIX)

    for candidate in candidates:
        version = version_from_dist_suffix(candidate, prefix)
        if version:
            if title == DEFAULT_TITLE and DEV_SUFFIX_RE.match(candidate):
                return DEV_TITLE, version
            return title, version

    raise ValueError(
        f"cannot derive update splash version from DIST_SUFFIX candidates: {candidates}"
    )


def sync_update_splash(
    output_dir: Path,
    title: str = DEFAULT_TITLE,
    dist_suffix: str | None = None,
    check: bool = False,
) -> bool:
    title, version = current_splash_metadata(dist_suffix, title=title)
    output_dir.mkdir(parents=True, exist_ok=True)

    # The committed frames are canonical. TTF rasterization differs slightly
    # between FreeType builds, so release checks validate their reviewed pixel
    # hashes instead of regenerating text on the CI host.
    if check:
        return manifest_matches(output_dir, title, version)

    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        expected_dir = Path(directory)
        expected_paths = generate_slideshow(title, version, expected_dir)
        expected_frames = {path.name: path.read_bytes() for path in expected_paths}

    actual_paths = sorted(output_dir.glob("frame_*.png"))
    actual_names = {path.name for path in actual_paths}
    expected_names = set(EXPECTED_FRAME_NAMES)
    stale_frames = sorted(
        path for path in actual_paths if path.name not in expected_names
    )
    missing_frames = expected_names - actual_names
    changed_frames = [
        path
        for path in actual_paths
        if path.name in expected_frames
        and not png_pixels_equal(path, expected_frames[path.name])
    ]
    changed_names = {path.name for path in changed_frames}
    for name in missing_frames | changed_names:
        (output_dir / name).write_bytes(expected_frames[name])
    for stale_frame in stale_frames:
        stale_frame.unlink()

    manifest = splash_manifest(output_dir, title, version)
    (output_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title", default=DEFAULT_TITLE)
    parser.add_argument("--dist-suffix")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)

    try:
        ok = sync_update_splash(
            args.output_dir,
            title=args.title,
            dist_suffix=args.dist_suffix,
            check=args.check,
        )
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if args.check and not ok:
        print("error: update splash is not synchronized", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
