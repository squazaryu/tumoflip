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
    r"(?:t-flppr-fw-(?:\d{3}-\d{3}|\d{3})|tmwhflpprarf\d{3}-\d{3}|"
    r"(?:t-)?dev-(?:\d{3}-\d{3}-\d{3}|\d{3}-\d{3}))"
)
DIST_SUFFIX_RE = re.compile(r'DIST_SUFFIX\s*=\s*"([^"]+)"')
STANDALONE_STABLE_VERSION_RE = re.compile(
    rf"^(?P<prefix>{re.escape(STABLE_PREFIX)})-(?P<release>\d{{3}})$"
)
STABLE_VERSION_RE = re.compile(
    rf"^(?P<prefix>{re.escape(STABLE_PREFIX)})-(?P<base>\d{{3}})-(?P<build>\d{{3}})$"
)
LEGACY_STABLE_VERSION_RE = re.compile(
    rf"^(?P<prefix>{LEGACY_STABLE_PREFIX})(?P<base>\d{{3}})-(?P<build>\d{{3}})$"
)
DEV_VERSION_RE = re.compile(
    r"^(?P<prefix>t-dev)-(?P<base>\d{3})-(?P<build>\d{3})-(?P<iteration>\d{3})$"
)
STANDALONE_DEV_VERSION_RE = re.compile(
    r"^(?P<prefix>t-dev)-(?P<release>\d{3})-(?P<iteration>\d{3})$"
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


def parse_dist_suffix(
    dist_suffix: str,
) -> tuple[str, str | None, str, str | None]:
    standalone_stable_match = STANDALONE_STABLE_VERSION_RE.match(dist_suffix)
    if standalone_stable_match:
        return (
            standalone_stable_match.group("prefix"),
            None,
            standalone_stable_match.group("release"),
            None,
        )

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

    standalone_dev_match = STANDALONE_DEV_VERSION_RE.match(dist_suffix)
    if standalone_dev_match:
        return (
            standalone_dev_match.group("prefix"),
            None,
            standalone_dev_match.group("release"),
            standalone_dev_match.group("iteration"),
        )

    raise ValueError(f"unsupported tumoflip DIST_SUFFIX: {dist_suffix}")


def is_stable_prefix(prefix: str) -> bool:
    return prefix in (STABLE_PREFIX, LEGACY_STABLE_PREFIX)


def sync_compact_readme_text(
    text: str, dist_suffix: str, release_tag: str | None = None
) -> str:
    """Update the compact README without rewriting historical identities.

    The current README intentionally keeps the stable identity visible while a
    development build is being prepared.  The original synchronizer predates
    that layout and replaced every version-shaped string, which turned stable
    documentation into a development snapshot.  Compact metadata has explicit
    Stable/Dev rows, so update only the row represented by ``dist_suffix`` and
    the branch artifact examples.
    """

    prefix, _, _, _ = parse_dist_suffix(dist_suffix)
    updated = text

    if is_stable_prefix(prefix):
        current_stable = re.compile(
            r"(^> \*\*Current stable:\*\* )`[^`]+`(?: \(`[^`]+`\))?",
            re.MULTILINE,
        )
        current_match = current_stable.search(updated)
        if current_match:
            semver = release_tag
            if semver is None:
                existing = re.search(r"\(`(v[^`]+)`\)", current_match.group(0))
                semver = existing.group(1) if existing else None
            semver_suffix = f" (`{semver}`)" if semver else ""
            replacement = f"{current_match.group(1)}`{dist_suffix}`{semver_suffix}"
            updated = current_stable.sub(replacement, updated, count=1)

        updated = re.sub(
            r"(^\| \*\*Stable\*\* \| )`[^`]+`",
            rf"\1`{dist_suffix}`",
            updated,
            count=1,
            flags=re.MULTILINE,
        )
    else:
        updated = re.sub(
            r"(^\| \*\*Dev\*\* \| )`[^`]+`(?: \([^)]*\))?",
            rf"\1`{dist_suffix}`",
            updated,
            count=1,
            flags=re.MULTILINE,
        )

    # The compact README labels these examples as the currently selected
    # branch build.  They must follow either a stable or development build,
    # while historical stable identities elsewhere remain untouched.
    updated = re.sub(
        r"flipper-z-f7-update-(?:t-flppr-fw-[0-9-]+|tmwhflpprarf[0-9-]+|t-dev-[0-9-]+)\.tgz",
        f"flipper-z-f7-update-{dist_suffix}.tgz",
        updated,
    )

    if release_tag:
        if not release_tag.startswith("v"):
            raise ValueError(f"release tag must start with v: {release_tag}")
        updated = re.sub(
            r"- Release: `v[^`]+` published release",
            f"- Release: `{release_tag}` published release",
            updated,
            count=1,
        )

    return updated


def sync_readme_text(
    text: str, dist_suffix: str, release_tag: str | None = None
) -> str:
    if "## Stable and Dev channels" in text and "## Version Scheme" not in text:
        return sync_compact_readme_text(text, dist_suffix, release_tag)

    prefix, base, build, iteration = parse_dist_suffix(dist_suffix)
    updated = README_VERSION_RE.sub(dist_suffix, text)
    # These lines describe the immutable first standalone stable release, not
    # the currently selected branch build. A dev bump must not rewrite history.
    updated = re.sub(
        r"(The first standalone stable line is\n)"
        r"`[^`]+`, published as SemVer `v1\.0\.0`,",
        r"\1`t-flppr-fw-001`, published as SemVer `v1.0.0`,",
        updated,
        count=1,
    )
    updated = re.sub(
        r"(- Rebranded firmware origin to `tumoflip` and distribution/version suffix to\n)"
        r"  `[^`]+`\.",
        r"\1  `t-flppr-fw-001`.",
        updated,
        count=1,
    )

    build_label = (
        "tumoflip internal build version"
        if base is not None
        else "standalone Tumoflip release number"
    )
    updated = re.sub(
        r"- `\d{3}`: (?:tumoflip internal build version|"
        r"standalone Tumoflip release number)\.",
        f"- `{build}`: {build_label}.",
        updated,
        count=1,
    )
    if iteration:
        iteration_label = (
            "development iteration inside the tumoflip internal build version"
            if base is not None
            else "development iteration inside the standalone Tumoflip release number"
        )
        updated, iteration_count = re.subn(
            r"- (?:`\d{3}`: development iteration inside the tumoflip internal build version|"
            r"`\d{3}`: development iteration inside the standalone Tumoflip release number|"
            r"`<iteration>`: monotonically increasing revision used only by `t-dev` builds)\.",
            f"- `{iteration}`: {iteration_label}.",
            updated,
            count=1,
        )
        if iteration_count == 0:
            build_line = f"- `{build}`: {build_label}."
            updated = updated.replace(
                build_line,
                f"{build_line}\n- `{iteration}`: {iteration_label}.",
                1,
            )
    else:
        # A stable identity has no development iteration. Keep the version
        # scheme documented without carrying the promoted dev build's final
        # iteration into stable release metadata.
        updated = re.sub(
            r"- `\d{3}`: development iteration inside the "
            r"(?:tumoflip internal build version|"
            r"standalone Tumoflip release number)\.",
            "- `<iteration>`: monotonically increasing revision used only by "
            "`t-dev` builds.",
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
            r"- Target stable SemVer: `v[^`]+`",
            f"- Target stable SemVer: `{release_tag}`",
            updated,
            count=1,
        )
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
    if base is not None and f"- `{base}`: upstream Unleashed base version." not in updated:
        raise ValueError("ReadMe.md base version line was not updated as expected")
    if f"- `{build}`: {build_label}." not in updated:
        raise ValueError(
            "ReadMe.md release/build version line was not updated as expected"
        )
    if iteration and f"- `{iteration}`: {iteration_label}." not in updated:
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
