#!/usr/bin/env python3
"""Prepare a new Tumoflip stable identity without reusing legacy release names."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.tumoflip.bump_dev_version import replace_dist_suffix, unified_diff
from tools.tumoflip.sync_readme_version import (
    STABLE_PREFIX,
    parse_dist_suffix,
    read_dist_suffix,
    sync_readme_text,
)
from tools.tumoflip.sync_update_splash import DEFAULT_OUTPUT_DIR, sync_update_splash


def validate_component(name: str, value: str) -> str:
    if len(value) != 3 or not value.isdigit():
        raise ValueError(f"{name} must be three digits, got: {value}")
    return value


def next_component(name: str, value: str) -> str:
    current = int(validate_component(name, value))
    if current >= 999:
        raise ValueError(f"{name} overflow: 999")
    return f"{current + 1:03d}"


def compute_stable_version(
    current_suffix: str,
    *,
    set_suffix: str | None = None,
    base: str | None = None,
    build: str | None = None,
) -> str:
    _, current_base, current_build, _ = parse_dist_suffix(current_suffix)
    if set_suffix:
        prefix, selected_base, selected_build, iteration = parse_dist_suffix(set_suffix)
        if prefix != STABLE_PREFIX or iteration is not None:
            raise ValueError(
                f"expected {STABLE_PREFIX} stable version, got: {set_suffix}"
            )
        if selected_base is None:
            return f"{STABLE_PREFIX}-{selected_build}"
        return f"{STABLE_PREFIX}-{selected_base}-{selected_build}"

    selected_base = validate_component("base", base) if base else current_base
    if build:
        selected_build = validate_component("build", build)
    elif current_base is None:
        # Standalone dev builds are named after the stable serial they started
        # from. Promotion always creates the next immutable stable serial.
        selected_build = next_component("standalone release", current_build)
    else:
        selected_build = current_build
    if selected_base is None:
        return f"{STABLE_PREFIX}-{selected_build}"
    return f"{STABLE_PREFIX}-{selected_base}-{selected_build}"


def apply_stable_version(
    repo_root: Path,
    new_suffix: str,
    *,
    dry_run: bool = False,
    update_splash: bool = True,
    release_tag: str | None = None,
) -> tuple[str, str]:
    repo_root = repo_root.resolve()
    old_suffix = read_dist_suffix(repo_root)
    expected_suffix = compute_stable_version(old_suffix, set_suffix=new_suffix)

    options_path = repo_root / "fbt_options.py"
    readme_path = repo_root / "ReadMe.md"
    options_before = options_path.read_text(encoding="utf-8")
    readme_before = readme_path.read_text(encoding="utf-8")
    options_after = replace_dist_suffix(options_before, expected_suffix)
    readme_after = sync_readme_text(
        readme_before,
        expected_suffix,
        release_tag=release_tag,
    )

    if dry_run:
        for path, before, after in (
            (options_path, options_before, options_after),
            (readme_path, readme_before, readme_after),
        ):
            if before != after:
                print(unified_diff(path, before, after), end="")
        return old_suffix, expected_suffix

    options_path.write_text(options_after, encoding="utf-8")
    readme_path.write_text(readme_after, encoding="utf-8")
    if update_splash:
        sync_update_splash(
            repo_root / DEFAULT_OUTPUT_DIR,
            dist_suffix=expected_suffix,
        )
    return old_suffix, expected_suffix


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--set", dest="set_suffix")
    parser.add_argument("--base")
    parser.add_argument("--build")
    parser.add_argument("--release-tag")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-splash", action="store_true")
    args = parser.parse_args()

    try:
        current = read_dist_suffix(args.repo_root)
        new_suffix = compute_stable_version(
            current,
            set_suffix=args.set_suffix,
            base=args.base,
            build=args.build,
        )
        old_suffix, new_suffix = apply_stable_version(
            args.repo_root,
            new_suffix,
            dry_run=args.dry_run,
            update_splash=not args.no_splash,
            release_tag=args.release_tag,
        )
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        return 2

    action = "would update" if args.dry_run else "updated"
    print(f"{action}: {old_suffix} -> {new_suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
