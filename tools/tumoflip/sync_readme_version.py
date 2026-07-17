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
STABLE_PREFIX = "t-flppr-fw"
LEGACY_STABLE_PREFIX = "tmwhflpprarf"
README_VERSION_RE = re.compile(
    r"(?:t-flppr-fw-\d{3}-\d{3}|tmwhflpprarf\d{3}-\d{3}|(?:t-)?dev-\d{3}-\d{3}-\d{3})"
)
DIST_SUFFIX_RE = re.compile(r'DIST_SUFFIX\s*=\s*"([^"]+)"')
STABLE_VERSION_RE = re.compile(
    rf"^(?P<prefix>{re.escape(STABLE_PREFIX)})-(?P<base>\d{{3}})-(?P<build>\d{{3}})$"
)
LEGACY_STABLE_VERSION_RE = re.compile(
    rf"^(?P<prefix>{LEGACY_STABLE_PREFIX})(?P<base>\d{{3}})-(?P<build>\d{{3}})$"
)
DEV_VERSION_RE = re.compile(
    r"^(?P<prefix>t-dev)-(?P<base>\d{3})-(?P<build>\d{3})-(?P<iteration>\d{3})$"
)


def read_dist_suffix(repo_root: Path = REPO_ROOT) -> str:
    env_suffix = os.environ.get("DIST_SUFFIX")
    if env_suffix:
        return env_suffix

    options = (repo_root / "fbt_options.py").read_text(encoding="utf-8")
    match = DIST_SUFFIX_RE.search(options)
    if not match:
        raise ValueError("DIST_SUFFIX not found in fbt_options.py")
    return match.group(1)


def parse_dist_suffix(dist_suffix: str) -> tuple[str, str, str, str | None]:
    stable_match = STABLE_VERSION_RE.match(dist_suffix)
    if not stable_match:
        stable_match = LEGACY_STABLE_VERSION_RE.match(dist_suffix)
    if stable_match:
        return (
            stable_match.group("prefix"),
            stable_match.group("base"),
            stable_match.group("build"),
            None,
        )

    dev_match = DEV_VERSION_RE.match(dist_suffix)
    if dev_match:
        return (
            dev_match.group("prefix"),
            dev_match.group("base"),
            dev_match.group("build"),
            dev_match.group("iteration"),
        )

    raise ValueError(f"unsupported tumoflip DIST_SUFFIX: {dist_suffix}")


def is_stable_prefix(prefix: str) -> bool:
    return prefix in (STABLE_PREFIX, LEGACY_STABLE_PREFIX)


def sync_readme_text(
    text: str, dist_suffix: str, release_tag: str | None = None
) -> str:
    prefix, base, build, iteration = parse_dist_suffix(dist_suffix)
    updated = README_VERSION_RE.sub(dist_suffix, text)

    updated = re.sub(
        r"- `\d{3}`: tumoflip internal build version\.",
        f"- `{build}`: tumoflip internal build version.",
        updated,
        count=1,
    )
    if iteration:
        updated = re.sub(
            r"- (?:`\d{3}`: development iteration inside the tumoflip internal build version|"
            r"`<iteration>`: monotonically increasing revision used only by `t-dev` builds)\.",
            f"- `{iteration}`: development iteration inside the tumoflip internal build version.",
            updated,
            count=1,
        )

    expected_channel = (
        "main stable line" if is_stable_prefix(prefix) else "dev experimental line"
    )
    updated = re.sub(
        r"- Release channel: `[^`]+`[^\n]*",
        f"- Release channel: `{expected_channel}`",
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

    if prefix == STABLE_PREFIX:
        expected_prefix_line = (
            "- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix."
        )
    elif prefix == LEGACY_STABLE_PREFIX:
        expected_prefix_line = (
            "- `tmwhflpprarf`: legacy stable prefix kept for existing releases."
        )
    else:
        expected_prefix_line = (
            "- `t-dev`: Tumoflip development build prefix for unstable builds."
        )
    if expected_prefix_line not in updated:
        raise ValueError(
            "ReadMe.md version scheme prefix line was not updated as expected"
        )
    if f"- `{base}`: upstream Unleashed base version." not in updated:
        raise ValueError("ReadMe.md base version line was not updated as expected")
    if f"- `{build}`: tumoflip internal build version." not in updated:
        raise ValueError(
            "ReadMe.md internal build version line was not updated as expected"
        )
    if iteration and (
        f"- `{iteration}`: development iteration inside the tumoflip internal build version."
        not in updated
    ):
        raise ValueError(
            "ReadMe.md development iteration line was not updated as expected"
        )

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
