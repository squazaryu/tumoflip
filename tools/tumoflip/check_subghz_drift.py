#!/usr/bin/env python3
"""Detect accidental drift between core Sub-GHz and ARF Sub-GHz copies."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


DEFAULT_MANIFEST = Path("tools/tumoflip/subghz_drift_manifest.txt")
CORE_ROOT = Path("applications/main/subghz")
ARF_ROOT = Path("applications_user/arf_subghz_full")


def load_manifest(path: Path) -> list[str]:
    entries: list[str] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("/") or "\\" in line or ".." in line.split("/"):
            raise ValueError(f"unsafe manifest path at line {line_number}: {line}")
        entries.append(line)

    duplicates = sorted({entry for entry in entries if entries.count(entry) > 1})
    if duplicates:
        raise ValueError(f"duplicate manifest entries: {', '.join(duplicates)}")
    return entries


def common_file_stats(repo_root: Path) -> tuple[int, int, int]:
    core_root = repo_root / CORE_ROOT
    arf_root = repo_root / ARF_ROOT
    common = 0
    matching = 0
    diverged = 0
    for core_path in sorted(core_root.rglob("*")):
        if not core_path.is_file():
            continue
        relative = core_path.relative_to(core_root)
        arf_path = arf_root / relative
        if not arf_path.is_file():
            continue
        common += 1
        if core_path.read_bytes() == arf_path.read_bytes():
            matching += 1
        else:
            diverged += 1
    return common, matching, diverged


def check_manifest(repo_root: Path, manifest_path: Path) -> list[str]:
    failures: list[str] = []
    for relative in load_manifest(manifest_path):
        core_path = repo_root / CORE_ROOT / relative
        arf_path = repo_root / ARF_ROOT / relative
        if not core_path.is_file():
            failures.append(f"missing core file: {relative}")
            continue
        if not arf_path.is_file():
            failures.append(f"missing ARF file: {relative}")
            continue
        if core_path.read_bytes() != arf_path.read_bytes():
            failures.append(f"drifted shared file: {relative}")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = args.manifest
    if not manifest_path.is_absolute():
        manifest_path = repo_root / manifest_path

    try:
        tracked = load_manifest(manifest_path)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    common, matching, diverged = common_file_stats(repo_root)
    failures = check_manifest(repo_root, manifest_path)

    print(
        "Sub-GHz drift check: "
        f"common={common}, byte_identical={matching}, intentionally_diverged={diverged}, "
        f"tracked={len(tracked)}"
    )
    if failures:
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        return 1

    print(f"OK: {len(tracked)} tracked Sub-GHz duplicate files match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
