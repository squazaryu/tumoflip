#!/usr/bin/env python3
"""Synchronize the tumoflip post-update splash with the firmware version."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import fbt_options

try:
    from tools.tumoflip.generate_update_splash import generate_slideshow
except ModuleNotFoundError:
    from generate_update_splash import generate_slideshow


DEFAULT_TITLE = "TMWHFLPPRARF"
DEFAULT_PREFIX = "tmwhflpprarf"
DEFAULT_OUTPUT_DIR = Path("assets/slideshow/tumoflip_update")


def version_from_dist_suffix(dist_suffix: str, prefix: str = DEFAULT_PREFIX) -> str | None:
    if not dist_suffix.startswith(prefix):
        return None
    version = dist_suffix.removeprefix(prefix)
    return version or None


def current_version(dist_suffix: str | None = None, prefix: str = DEFAULT_PREFIX) -> str:
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
            return version

    raise ValueError(
        f"cannot derive update splash version from DIST_SUFFIX candidates: {candidates}"
    )


def sync_update_splash(
    output_dir: Path,
    title: str = DEFAULT_TITLE,
    dist_suffix: str | None = None,
    check: bool = False,
) -> bool:
    version = current_version(dist_suffix)
    output_dir.mkdir(parents=True, exist_ok=True)

    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        expected_dir = Path(directory)
        expected_paths = generate_slideshow(title, version, expected_dir)
        expected_frames = {path.name: path.read_bytes() for path in expected_paths}

    actual_paths = sorted(output_dir.glob("frame_*.png"))
    actual_names = {path.name for path in actual_paths}
    expected_names = set(expected_frames)
    stale_frames = sorted(path for path in actual_paths if path.name not in expected_names)
    missing_frames = expected_names - actual_names
    changed_frames = [
        path
        for path in actual_paths
        if path.name in expected_frames and path.read_bytes() != expected_frames[path.name]
    ]
    in_sync = not stale_frames and not missing_frames and not changed_frames
    if check:
        return in_sync

    for name, content in expected_frames.items():
        (output_dir / name).write_bytes(content)
    for stale_frame in stale_frames:
        stale_frame.unlink()
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
