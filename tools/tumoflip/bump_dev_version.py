#!/usr/bin/env python3
"""Bump the Tumoflip development firmware version consistently."""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.tumoflip.sync_readme_version import (  # noqa: E402
    DIST_SUFFIX_RE,
    is_stable_prefix,
    parse_dist_suffix,
    read_dist_suffix,
    sync_readme_text,
)
from tools.tumoflip.sync_update_splash import (  # noqa: E402
    DEFAULT_OUTPUT_DIR,
    sync_update_splash,
)


VERSION_COMPONENT_RE = re.compile(r"^\d{3}$")


@dataclass(frozen=True)
class DevVersion:
    base: str
    build: str
    iteration: str

    @classmethod
    def parse(cls, version: str) -> "DevVersion":
        prefix, base, build, iteration = parse_dist_suffix(version)
        if prefix != "t-dev" or iteration is None:
            raise ValueError(f"expected t-dev version, got: {version}")
        return cls(base=base, build=build, iteration=iteration)

    def format(self) -> str:
        return f"t-dev-{self.base}-{self.build}-{self.iteration}"


def validate_component(name: str, value: str) -> str:
    if not VERSION_COMPONENT_RE.fullmatch(value):
        raise ValueError(f"{name} must be three digits, got: {value}")
    return value


def next_iteration(iteration: str) -> str:
    value = int(iteration) + 1
    if value > 999:
        raise ValueError("development iteration overflow: 999")
    return f"{value:03d}"


def compute_dev_version(
    current_suffix: str,
    *,
    set_suffix: str | None = None,
    base: str | None = None,
    build: str | None = None,
    iteration: str | None = None,
) -> str:
    if set_suffix:
        return DevVersion.parse(set_suffix).format()

    prefix, current_base, current_build, current_iteration = parse_dist_suffix(
        current_suffix
    )
    new_base = validate_component("base", base) if base else current_base
    new_build = validate_component("build", build) if build else current_build

    if iteration:
        new_iteration = validate_component("iteration", iteration)
    elif is_stable_prefix(prefix):
        new_iteration = "001"
    else:
        current = DevVersion(current_base, current_build, current_iteration or "001")
        if new_base != current.base or new_build != current.build:
            new_iteration = "001"
        else:
            new_iteration = next_iteration(current.iteration)

    return DevVersion(new_base, new_build, new_iteration).format()


def replace_dist_suffix(text: str, new_suffix: str) -> str:
    def replace(match: re.Match[str]) -> str:
        return f'{match.group(0).split("=")[0].rstrip()} = "{new_suffix}"'

    updated, count = DIST_SUFFIX_RE.subn(replace, text, count=1)
    if count != 1:
        raise ValueError("DIST_SUFFIX not found in fbt_options.py")
    return updated


def unified_diff(path: Path, before: str, after: str) -> str:
    return "".join(
        difflib.unified_diff(
            before.splitlines(keepends=True),
            after.splitlines(keepends=True),
            fromfile=str(path),
            tofile=f"{path} (expected)",
        )
    )


def apply_dev_version(
    repo_root: Path,
    new_suffix: str,
    *,
    dry_run: bool = False,
    update_splash: bool = True,
) -> tuple[str, str]:
    repo_root = repo_root.resolve()
    old_suffix = read_dist_suffix(repo_root)
    parse_dist_suffix(old_suffix)
    DevVersion.parse(new_suffix)

    options_path = repo_root / "fbt_options.py"
    readme_path = repo_root / "ReadMe.md"

    options_before = options_path.read_text(encoding="utf-8")
    readme_before = readme_path.read_text(encoding="utf-8")

    options_after = replace_dist_suffix(options_before, new_suffix)
    readme_after = sync_readme_text(readme_before, new_suffix)

    if dry_run:
        for path, before, after in (
            (options_path, options_before, options_after),
            (readme_path, readme_before, readme_after),
        ):
            if before != after:
                print(unified_diff(path, before, after), end="")
        return old_suffix, new_suffix

    options_path.write_text(options_after, encoding="utf-8")
    readme_path.write_text(readme_after, encoding="utf-8")

    if update_splash:
        sync_update_splash(
            repo_root / DEFAULT_OUTPUT_DIR,
            dist_suffix=new_suffix,
        )

    return old_suffix, new_suffix


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--set", dest="set_suffix", help="exact t-dev version to apply")
    parser.add_argument("--base", help="three-digit Unleashed base version")
    parser.add_argument("--build", help="three-digit Tumoflip internal build version")
    parser.add_argument("--iteration", help="three-digit development iteration")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-splash", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        current_suffix = read_dist_suffix(args.repo_root)
        new_suffix = compute_dev_version(
            current_suffix,
            set_suffix=args.set_suffix,
            base=args.base,
            build=args.build,
            iteration=args.iteration,
        )
        old_suffix, applied_suffix = apply_dev_version(
            args.repo_root,
            new_suffix,
            dry_run=args.dry_run,
            update_splash=not args.no_splash,
        )
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    action = "would update" if args.dry_run else "updated"
    print(f"{action}: {old_suffix} -> {applied_suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
