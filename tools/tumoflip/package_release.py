#!/usr/bin/env python3
"""Build package-only Tumoflip SD package assets without bumping firmware."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

try:
    from .make_packages_zip import build_packages_zip
    from .validate_release import (
        ARF_LEGACY_PATHS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        require_file,
        sync_extapp_package_exports,
        validate_layout,
    )
except ImportError:
    from make_packages_zip import build_packages_zip
    from validate_release import (
        ARF_LEGACY_PATHS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        require_file,
        sync_extapp_package_exports,
        validate_layout,
    )


def git_output(repo_root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def git_identity(repo_root: Path) -> tuple[str, str, bool]:
    commit = git_output(repo_root, "rev-parse", "HEAD")
    short = git_output(repo_root, "rev-parse", "--short", "HEAD")
    dirty = bool(git_output(repo_root, "status", "--porcelain"))
    return commit, short, dirty


def package_release_id(version: str, short_commit: str, dirty: bool) -> str:
    suffix = "-dirty" if dirty else ""
    return f"{version}-packages-{short_commit}{suffix}"


def build_package_release(
    repo_root: Path,
    build_dir: Path,
    out_dir: Path,
    package_id: str | None = None,
    target_release_tag: str | None = None,
) -> dict[str, object]:
    firmware_json = json.loads(
        require_file(build_dir / "firmware.json", "firmware metadata").read_text(
            encoding="utf-8"
        )
    )
    if firmware_json["firmware_target"] != 7:
        raise ValidationError("firmware.json target is not Flipper Zero (7)")

    resources = require_file(
        build_dir / "resources/Manifest", "resource manifest"
    ).parent
    install_static_sd_resources(repo_root, resources)
    synced = sync_extapp_package_exports(build_dir, resources)
    validate_layout(resources)
    packages = package_entries(resources)
    commit, short, dirty = git_identity(repo_root)
    version = firmware_json["firmware_version"]
    resolved_package_id = package_id or package_release_id(version, short, dirty)

    manifest: dict[str, object] = {
        "schema": 2,
        "firmware": {
            "name": "tumoflip",
            "version": version,
            "target": 7,
            "api": api_version(repo_root / "targets/f7/api_symbols.csv"),
        },
        "package_release": {
            "type": "package-only",
            "id": resolved_package_id,
            "source_commit": commit,
            "source_dirty": dirty,
            "target_release_tag": target_release_tag,
            "firmware_flash_unchanged": True,
            "synced_extapps": synced,
        },
        "artifacts": {},
        "packages": packages,
        "cleanup": [
            {"legacy": legacy, "canonical": canonical}
            for legacy, canonical in sorted(ARF_LEGACY_PATHS.items())
        ],
    }
    manifest["release_id"] = manifest_release_id(manifest)

    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "tumoflip-packages.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    build_packages_zip(manifest, resources, out_dir / "tumoflip-packages.zip")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[2]
    )
    parser.add_argument("--build-dir", type=Path, default=Path("build/f7-firmware-C"))
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--package-id")
    parser.add_argument("--target-release-tag")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    build_dir = (repo_root / args.build_dir).resolve()
    try:
        firmware = json.loads(
            require_file(build_dir / "firmware.json", "firmware metadata").read_text(
                encoding="utf-8"
            )
        )
        out_dir = (
            (repo_root / args.out_dir).resolve()
            if args.out_dir
            else repo_root
            / "dist/f7-C"
            / f"f7-update-{firmware['firmware_version']}"
        )
        manifest = build_package_release(
            repo_root,
            build_dir,
            out_dir,
            package_id=args.package_id,
            target_release_tag=args.target_release_tag,
        )
    except (
        OSError,
        KeyError,
        ValueError,
        subprocess.CalledProcessError,
        ValidationError,
    ) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    release = manifest["package_release"]
    groups = ", ".join(
        f"{name}={len(entries)}" for name, entries in sorted(manifest["packages"].items())
    )
    print(
        "OK: package-only assets written; "
        f"id={release['id']}; firmware={manifest['firmware']['version']}; "
        f"groups={groups}; release_id={manifest['release_id']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
