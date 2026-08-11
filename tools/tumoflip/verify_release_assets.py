#!/usr/bin/env python3
"""Verify downloaded release assets against an existing SHA-256 sums file."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ReleaseAssetError(Exception):
    """Raised when a checksum ledger or downloaded release asset is invalid."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256_sums(path: Path) -> dict[str, str]:
    """Parse a strict sha256sum ledger keyed by safe asset basenames."""
    if not path.is_file():
        raise ReleaseAssetError(f"SHA-256 sums file is missing: {path}")

    entries: dict[str, str] = {}
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not raw_line:
            raise ReleaseAssetError(
                f"Blank line in SHA-256 sums file at line {line_number}"
            )
        parts = raw_line.split(maxsplit=1)
        if len(parts) != 2:
            raise ReleaseAssetError(
                f"Malformed SHA-256 entry at line {line_number}"
            )
        digest, filename = parts
        if filename.startswith("*"):
            filename = filename[1:]
        if not SHA256_RE.fullmatch(digest):
            raise ReleaseAssetError(
                f"Invalid SHA-256 digest at line {line_number}"
            )
        if (
            not filename
            or filename != Path(filename).name
            or filename in {".", ".."}
            or "/" in filename
            or "\\" in filename
            or "\x00" in filename
        ):
            raise ReleaseAssetError(
                f"Unsafe release asset name at line {line_number}: {filename!r}"
            )
        if filename in entries:
            raise ReleaseAssetError(f"Duplicate SHA-256 entry for {filename}")
        entries[filename] = digest

    if not entries:
        raise ReleaseAssetError("SHA-256 sums file is empty")
    return entries


def verify_release_assets(checksums: Path, assets: list[Path]) -> None:
    """Require the ledger to describe exactly the requested assets and verify each."""
    if not assets:
        raise ReleaseAssetError("No release assets were supplied for verification")

    entries = parse_sha256_sums(checksums)
    requested: dict[str, Path] = {}
    for asset in assets:
        if asset.name in requested:
            raise ReleaseAssetError(f"Duplicate requested release asset: {asset.name}")
        requested[asset.name] = asset

    if set(entries) != set(requested):
        missing = sorted(set(requested) - set(entries))
        extra = sorted(set(entries) - set(requested))
        raise ReleaseAssetError(
            f"SHA-256 asset set differs; missing={missing}, extra={extra}"
        )

    for filename, asset in requested.items():
        if not asset.is_file():
            raise ReleaseAssetError(f"Release asset is missing: {asset}")
        actual = sha256(asset)
        expected = entries[filename]
        if actual != expected:
            raise ReleaseAssetError(
                f"SHA-256 mismatch for {filename}: {actual} != {expected}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checksums", type=Path)
    parser.add_argument("assets", nargs="+", type=Path)
    args = parser.parse_args()

    try:
        verify_release_assets(args.checksums, args.assets)
    except (OSError, UnicodeError, ReleaseAssetError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"OK: verified {len(args.assets)} existing release assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
