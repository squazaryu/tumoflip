#!/usr/bin/env python3
"""Reject reused or non-monotonic Tumoflip stable release identities."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections.abc import Collection
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.tumoflip.sync_readme_version import STABLE_PREFIX, parse_dist_suffix


SEMVER_TAG_RE = re.compile(
    r"^v(?P<major>0|[1-9]\d*)\."
    r"(?P<minor>0|[1-9]\d*)\."
    r"(?P<patch>0|[1-9]\d*)$"
)


@dataclass(frozen=True, order=True)
class StableSemVer:
    major: int
    minor: int
    patch: int


def parse_stable_tag(tag: str) -> StableSemVer:
    match = SEMVER_TAG_RE.fullmatch(tag)
    if not match:
        raise ValueError(f"stable release tag must be vMAJOR.MINOR.PATCH: {tag}")
    return StableSemVer(
        major=int(match.group("major")),
        minor=int(match.group("minor")),
        patch=int(match.group("patch")),
    )


def parse_stable_serial(version: str) -> int:
    prefix, base, build, iteration = parse_dist_suffix(version)
    if prefix != STABLE_PREFIX or base is not None or iteration is not None:
        raise ValueError(
            f"stable firmware identity must be {STABLE_PREFIX}-NNN: {version}"
        )
    return int(build)


def validate_new_stable_release(
    *,
    release_tag: str,
    firmware_version: str,
    previous_release_tag: str,
    previous_firmware_version: str,
    existing_release_tags: Collection[str] = (),
) -> None:
    current_tag = parse_stable_tag(release_tag)
    previous_tag = parse_stable_tag(previous_release_tag)
    if current_tag <= previous_tag:
        raise ValueError(
            f"stable tag must advance beyond {previous_release_tag}: {release_tag}"
        )

    current_serial = parse_stable_serial(firmware_version)
    previous_serial = parse_stable_serial(previous_firmware_version)
    expected_serial = previous_serial + 1
    if expected_serial > 999:
        raise ValueError("standalone stable serial overflow: 999")
    if current_serial != expected_serial:
        raise ValueError(
            "stable firmware serial must advance exactly once: "
            f"expected {STABLE_PREFIX}-{expected_serial:03d}, got {firmware_version}"
        )

    same_line = (
        current_tag.major == previous_tag.major
        and current_tag.minor == previous_tag.minor
    )
    if same_line:
        consumed_tags = {
            f"v{previous_tag.major}.{previous_tag.minor}.{patch}"
            for patch in range(previous_tag.patch + 1, current_tag.patch)
        }
        missing_tags = sorted(consumed_tags.difference(existing_release_tags))
        if missing_tags:
            raise ValueError(
                "stable patch may skip only tags that already exist: "
                + ", ".join(missing_tags)
            )


def repository_release_tags(repo_root: Path) -> set[str]:
    result = subprocess.run(
        ["git", "tag", "--list", "v*"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return {line for line in result.stdout.splitlines() if line}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-tag", required=True)
    parser.add_argument("--firmware-version", required=True)
    parser.add_argument("--previous-release-tag", required=True)
    parser.add_argument("--previous-firmware-version", required=True)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    args = parser.parse_args()

    try:
        validate_new_stable_release(
            release_tag=args.release_tag,
            firmware_version=args.firmware_version,
            previous_release_tag=args.previous_release_tag,
            previous_firmware_version=args.previous_firmware_version,
            existing_release_tags=repository_release_tags(args.repo_root),
        )
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"error: {error}")
        return 2

    print(
        "OK: immutable stable release "
        f"{args.previous_release_tag}/{args.previous_firmware_version} -> "
        f"{args.release_tag}/{args.firmware_version}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
