#!/usr/bin/env python3
"""Build tumoflip-packages.zip from a schema-v2 manifest + the resources tree.

The companion iOS app (squazaryu/unleashed-companion, issue #8) installs SD package
files atomically (stage -> verify -> rename, with rollback). It reads the manifest and
needs the raw file bytes. Rather than have the app decode the firmware's heatshrink
(HSDS) resources archive, we publish a plain DEFLATE zip containing exactly the
manifest's `source` files; each zip entry path equals its manifest `source` 1:1.

Hardening:
  * every `source` is validated (relative POSIX path, no traversal/absolute/duplicate),
    and is confirmed to resolve inside the resources root;
  * each file's SHA-256 is verified against the manifest before it is added;
  * entries use sorted paths, fixed timestamps, normalized Unix permissions, and a
    fixed compression level so identical package content produces identical bytes;
  * the archive is written to a temp file in the destination directory and only
    atomically renamed into place after the WHOLE archive is built and verified, so a
    failure never leaves a partial or stale package zip.

Usage:
    python3 tools/tumoflip/make_packages_zip.py <manifest.json> <resources_root> <out.zip>
"""

from __future__ import annotations

import hashlib
import json
import os
import stat
import sys
import tempfile
import zipfile
from pathlib import Path


ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_FILE_MODE = stat.S_IFREG | 0o644
ZIP_COMPRESSION_LEVEL = 9


class PackageError(Exception):
    """Raised on an invalid manifest, unsafe path, missing source, or hash mismatch."""


def _check_source_path(source: str) -> None:
    if not isinstance(source, str) or not source or source != source.strip():
        raise PackageError(f"invalid source path: {source!r}")
    if "\\" in source or "\x00" in source:
        raise PackageError(f"invalid source path: {source!r}")
    # Check the RAW segments (PurePosixPath would silently normalise away "." and "//").
    # An empty segment means a leading/trailing/double slash (incl. absolute paths).
    segments = source.split("/")
    if any(segment in ("", ".", "..") for segment in segments):
        raise PackageError(f"path traversal or unsafe segment in source: {source}")


def iter_package_sources(manifest: dict) -> list[tuple[str, str]]:
    """Return [(source, sha256)] for every package file, validating schema, path
    safety, and duplicate sources up front."""
    if manifest.get("schema") != 2:
        raise PackageError(f"unsupported manifest schema: {manifest.get('schema')!r}")
    out: list[tuple[str, str]] = []
    seen: set[str] = set()
    for _group, files in sorted(manifest.get("packages", {}).items()):
        for entry in files:
            source = entry["source"]
            sha = entry["sha256"]
            _check_source_path(source)
            if source in seen:
                raise PackageError(f"duplicate source in manifest: {source}")
            seen.add(source)
            out.append((source, sha))
    return sorted(out, key=lambda item: item[0])


def _zip_info(source: str) -> zipfile.ZipInfo:
    """Return normalized metadata for a reproducible package entry."""
    info = zipfile.ZipInfo(source, date_time=ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3  # Unix mode bits in external_attr.
    info.external_attr = ZIP_FILE_MODE << 16
    info.internal_attr = 0
    info.extra = b""
    info.comment = b""
    return info


def build_packages_zip(manifest: dict, resources_root, out_path) -> int:
    """Write `out_path` containing every manifest source file. Returns the file count.
    Atomic: a temp file is renamed into place only after full verification."""
    resources_root = Path(resources_root)
    out_path = Path(out_path)
    sources = iter_package_sources(manifest)
    root_resolved = resources_root.resolve()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    handle, tmp_name = tempfile.mkstemp(
        prefix=out_path.name + ".", suffix=".tmp", dir=str(out_path.parent)
    )
    os.close(handle)
    tmp_path = Path(tmp_name)
    try:
        with zipfile.ZipFile(
            tmp_path,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=ZIP_COMPRESSION_LEVEL,
        ) as archive:
            for source, sha in sources:
                file_path = resources_root / source
                resolved = file_path.resolve()
                if resolved != root_resolved and root_resolved not in resolved.parents:
                    raise PackageError(f"source escapes resources root: {source}")
                if not file_path.is_file():
                    raise PackageError(f"missing source: {source}")
                data = file_path.read_bytes()
                actual = hashlib.sha256(data).hexdigest()
                if actual != sha:
                    raise PackageError(
                        f"sha256 mismatch for {source}: {actual} != {sha}"
                    )
                archive.writestr(
                    _zip_info(source),
                    data,
                    compress_type=zipfile.ZIP_DEFLATED,
                    compresslevel=ZIP_COMPRESSION_LEVEL,
                )
        os.replace(tmp_path, out_path)  # atomic publish, only after full verification
    except BaseException:
        try:
            tmp_path.unlink()
        except FileNotFoundError:
            pass
        raise
    return len(sources)


def main() -> int:
    if len(sys.argv) != 4:
        sys.stderr.write(__doc__)
        return 2
    manifest_path, resources_root, out_zip = sys.argv[1:4]
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    try:
        count = build_packages_zip(manifest, resources_root, out_zip)
    except PackageError as error:
        sys.stderr.write(f"error: {error}\n")
        return 1
    sys.stdout.write(f"wrote {out_zip}: {count} files\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
