#!/usr/bin/env python3
"""Synchronize ReadMe.md version strings from the firmware build metadata."""

from __future__ import annotations

import argparse
import difflib
import os
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
README_VERSION_RE = re.compile(r"tmwhflpprarf\d{3}-\d{3}")
DIST_SUFFIX_RE = re.compile(r'DIST_SUFFIX\s*=\s*"([^"]+)"')
VERSION_RE = re.compile(r"^(?P<prefix>tmwhflpprarf)(?P<base>\d{3})-(?P<build>\d{3})$")


def read_dist_suffix(repo_root: Path = REPO_ROOT) -> str:
    env_suffix = os.environ.get("DIST_SUFFIX")
    if env_suffix:
        return env_suffix

    options = (repo_root / "fbt_options.py").read_text(encoding="utf-8")
    match = DIST_SUFFIX_RE.search(options)
    if not match:
        raise ValueError("DIST_SUFFIX not found in fbt_options.py")
    return match.group(1)


def parse_dist_suffix(dist_suffix: str) -> tuple[str, str, str]:
    match = VERSION_RE.match(dist_suffix)
    if not match:
        raise ValueError(f"unsupported tumoflip DIST_SUFFIX: {dist_suffix}")
    return match.group("prefix"), match.group("base"), match.group("build")


def sync_readme_text(text: str, dist_suffix: str, release_tag: str | None = None) -> str:
    prefix, base, build = parse_dist_suffix(dist_suffix)
    updated = README_VERSION_RE.sub(dist_suffix, text)

    updated = re.sub(
        r"- `\d{3}`: tumoflip internal build version\.",
        f"- `{build}`: tumoflip internal build version.",
        updated,
        count=1,
    )

    if release_tag:
        if not release_tag.startswith("v"):
            raise ValueError(f"release tag must start with v: {release_tag}")
        updated = re.sub(
            r"- Release: `v[^`]+` published release \(hardware validation in progress\)",
            f"- Release: `{release_tag}` published release (hardware validation in progress)",
            updated,
            count=1,
        )

    expected_prefix_line = (
        f"- `{prefix}`: tumoflip firmware name shown as the installed firmware"
    )
    if expected_prefix_line not in updated:
        raise ValueError("ReadMe.md version scheme prefix line was not updated as expected")
    if f"- `{base}`: upstream Unleashed base version." not in updated:
        raise ValueError("ReadMe.md base version line was not updated as expected")
    if f"- `{build}`: tumoflip internal build version." not in updated:
        raise ValueError("ReadMe.md internal build version line was not updated as expected")

    return updated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--readme", type=Path, default=Path("ReadMe.md"))
    parser.add_argument("--dist-suffix", default=None)
    parser.add_argument("--release-tag", default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    readme = args.readme
    if not readme.is_absolute():
        readme = repo_root / readme

    dist_suffix = args.dist_suffix or read_dist_suffix(repo_root)
    release_tag = args.release_tag
    if release_tag is None and os.environ.get("GITHUB_REF_NAME", "").startswith("v"):
        release_tag = os.environ["GITHUB_REF_NAME"]

    original = readme.read_text(encoding="utf-8")
    updated = sync_readme_text(original, dist_suffix, release_tag)

    if original == updated:
        return 0

    if args.check:
        diff = difflib.unified_diff(
            original.splitlines(keepends=True),
            updated.splitlines(keepends=True),
            fromfile=str(readme),
            tofile=f"{readme} (expected)",
        )
        sys.stderr.writelines(diff)
        return 1

    readme.write_text(updated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
