#!/usr/bin/env python3
"""Build package-only Tumoflip SD package assets without bumping firmware."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

try:
    from .make_packages_zip import (
        PackageError,
        build_packages_zip,
        iter_package_sources,
    )
    from .validate_release import (
        PACKAGE_ONLY_PACKAGE_FILES,
        PACKAGE_ONLY_PACKAGE_GROUPS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        require_file,
        release_cleanup_entries,
        sync_extapp_package_exports,
        validate_layout,
    )
except ImportError:
    from make_packages_zip import (
        PackageError,
        build_packages_zip,
        iter_package_sources,
    )
    from validate_release import (
        PACKAGE_ONLY_PACKAGE_FILES,
        PACKAGE_ONLY_PACKAGE_GROUPS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        require_file,
        release_cleanup_entries,
        sync_extapp_package_exports,
        validate_layout,
    )


MAX_TARGET_PACKAGE_ENTRIES = 512
MAX_TARGET_PACKAGE_FILE_BYTES = 16 * 1024 * 1024
MAX_TARGET_PACKAGE_TOTAL_BYTES = 64 * 1024 * 1024
CATALOG_RELEASE_TAG = re.compile(r"^fw-packages-(stable|dev)-([0-9]{3})$")
CATALOG_BASELINES_PATH = Path("tools/tumoflip/package_catalog_baselines.json")


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


def catalog_release_identity(tag: str) -> tuple[str, int]:
    match = CATALOG_RELEASE_TAG.fullmatch(tag)
    if match is None:
        raise ValidationError(
            "Independent package release tag must look like "
            "fw-packages-stable-001 or fw-packages-dev-001"
        )
    revision = int(match.group(2))
    if revision < 1:
        raise ValidationError("Independent package revision must be greater than zero")
    return match.group(1), revision


def catalog_baseline(
    repo_root: Path,
    channel: str,
) -> dict[str, object]:
    path = require_file(
        repo_root / CATALOG_BASELINES_PATH,
        "package catalog baselines",
    )
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValidationError(f"Package catalog baselines are invalid: {error}") from error
    if document.get("schema") != 1:
        raise ValidationError("Package catalog baselines must use schema 1")
    baseline = document.get(channel)
    if not isinstance(baseline, dict):
        raise ValidationError(f"Package catalog baseline is missing for {channel}")
    expected_types = {
        "release_tag": str,
        "firmware_version": str,
        "api": str,
        "target": int,
    }
    for key, expected_type in expected_types.items():
        value = baseline.get(key)
        if (
            not isinstance(value, expected_type)
            or isinstance(value, bool)
            or (isinstance(value, str) and not value.strip())
        ):
            raise ValidationError(
                f"Package catalog baseline {channel}.{key} is invalid"
            )
    return baseline


def resolve_target_firmware(
    repo_root: Path,
    source_firmware: dict[str, object],
    target_manifest: dict[str, object] | None,
) -> dict[str, object]:
    source_api = api_version(repo_root / "targets/f7/api_symbols.csv")
    if target_manifest is None:
        return {
            "name": "tumoflip",
            "version": source_firmware["firmware_version"],
            "target": 7,
            "api": source_api,
        }

    if target_manifest.get("schema") != 2:
        raise ValidationError("Target package manifest must use schema 2")
    target_firmware = target_manifest.get("firmware")
    if not isinstance(target_firmware, dict):
        raise ValidationError("Target package manifest firmware identity is missing")
    if target_firmware.get("name") != "tumoflip":
        raise ValidationError("Target package manifest is not Tumoflip")
    if target_firmware.get("target") != 7:
        raise ValidationError("Target package manifest is not for Flipper Zero")
    version = target_firmware.get("version")
    if not isinstance(version, str) or not version.strip():
        raise ValidationError("Target package manifest firmware version is invalid")
    target_api = target_firmware.get("api")
    if target_api != source_api:
        raise ValidationError(
            f"Target firmware API {target_api!r} does not match package build API "
            f"{source_api!r}"
        )
    return dict(target_firmware)


def validate_target_release_id(target_manifest: dict[str, object]) -> None:
    release_id = target_manifest.get("release_id")
    if (
        not isinstance(release_id, str)
        or len(release_id) != 64
        or any(character not in "0123456789abcdef" for character in release_id)
    ):
        raise ValidationError("Target package manifest release ID is invalid")
    unsigned = copy.deepcopy(target_manifest)
    unsigned.pop("release_id", None)
    actual = manifest_release_id(unsigned)
    if actual != release_id:
        raise ValidationError(
            f"Target package manifest release ID differs: {release_id} != {actual}"
        )


def _validated_target_entries(
    target_manifest: dict[str, object],
) -> dict[str, dict[str, object]]:
    """Return schema-v2 package entries keyed by source after strict validation."""
    try:
        sources = iter_package_sources(target_manifest)
    except (KeyError, TypeError, PackageError) as error:
        raise ValidationError(f"Target package manifest is invalid: {error}") from error
    if not sources:
        raise ValidationError("Target package manifest has no package files")
    if len(sources) > MAX_TARGET_PACKAGE_ENTRIES:
        raise ValidationError(
            "Target package manifest contains too many files: "
            f"{len(sources)} > {MAX_TARGET_PACKAGE_ENTRIES}"
        )

    packages = target_manifest.get("packages")
    if not isinstance(packages, dict):
        raise ValidationError("Target package manifest packages are missing")
    by_source: dict[str, dict[str, object]] = {}
    seen_targets: set[str] = set()
    total_bytes = 0
    for group, raw_entries in packages.items():
        if not isinstance(group, str) or not group or not isinstance(raw_entries, list):
            raise ValidationError("Target package manifest groups are invalid")
        for raw_entry in raw_entries:
            if not isinstance(raw_entry, dict):
                raise ValidationError("Target package manifest entry is invalid")
            source = raw_entry.get("source")
            target = raw_entry.get("target")
            size = raw_entry.get("bytes")
            sha = raw_entry.get("sha256")
            digest = raw_entry.get("md5")
            if not isinstance(source, str) or source in by_source:
                raise ValidationError(
                    f"Target package manifest has a duplicate source: {source!r}"
                )
            if target != f"/ext/{source}" or target in seen_targets:
                raise ValidationError(
                    f"Target package manifest has an invalid target: {target!r}"
                )
            if (
                not isinstance(size, int)
                or isinstance(size, bool)
                or size <= 0
                or size > MAX_TARGET_PACKAGE_FILE_BYTES
            ):
                raise ValidationError(
                    f"Target package file size is invalid for {source}: {size!r}"
                )
            if (
                not isinstance(sha, str)
                or len(sha) != 64
                or any(character not in "0123456789abcdef" for character in sha)
            ):
                raise ValidationError(f"Target package SHA-256 is invalid for {source}")
            if (
                not isinstance(digest, str)
                or len(digest) != 32
                or any(character not in "0123456789abcdef" for character in digest)
            ):
                raise ValidationError(f"Target package MD5 is invalid for {source}")
            total_bytes += size
            if total_bytes > MAX_TARGET_PACKAGE_TOTAL_BYTES:
                raise ValidationError(
                    "Target package contents exceed the uncompressed size limit"
                )
            by_source[source] = raw_entry
            seen_targets.add(target)

    if set(by_source) != {source for source, _sha in sources}:
        raise ValidationError("Target package manifest source index is inconsistent")
    return by_source


def materialize_target_package(
    target_manifest: dict[str, object],
    target_package_zip: Path,
    resources: Path,
) -> None:
    """Verify the published package ZIP and copy its exact payload into resources."""
    entries = _validated_target_entries(target_manifest)
    try:
        with zipfile.ZipFile(target_package_zip) as archive:
            infos = archive.infolist()
            names = [info.filename for info in infos]
            if len(names) != len(set(names)):
                raise ValidationError("Target package ZIP contains duplicate paths")
            if set(names) != set(entries):
                missing = sorted(set(entries) - set(names))
                extra = sorted(set(names) - set(entries))
                raise ValidationError(
                    f"Target package ZIP contents differ from manifest; "
                    f"missing={missing}, extra={extra}"
                )

            for info in infos:
                entry = entries[info.filename]
                expected_size = int(entry["bytes"])
                if info.is_dir() or info.file_size != expected_size:
                    raise ValidationError(
                        f"Target package ZIP size differs for {info.filename}: "
                        f"{info.file_size} != {expected_size}"
                    )
                unix_type = (info.external_attr >> 16) & 0o170000
                if unix_type not in (0, 0o100000):
                    raise ValidationError(
                        f"Target package ZIP entry is not a regular file: {info.filename}"
                    )
                data = archive.read(info)
                actual_sha = hashlib.sha256(data).hexdigest()
                actual_md5 = hashlib.md5(data).hexdigest()
                if actual_sha != entry["sha256"] or actual_md5 != entry["md5"]:
                    raise ValidationError(
                        f"Target package ZIP digest differs for {info.filename}"
                    )
                output = resources / info.filename
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(data)
    except (
        OSError,
        EOFError,
        NotImplementedError,
        RuntimeError,
        zipfile.BadZipFile,
        zipfile.LargeZipFile,
    ) as error:
        raise ValidationError(f"Target package ZIP is invalid: {error}") from error


def merge_package_only_entries(
    target_manifest: dict[str, object],
    resources: Path,
) -> dict[str, list[dict[str, object]]]:
    """Preserve all published entries and replace only explicit package-only files."""
    packages = target_manifest.get("packages")
    if not isinstance(packages, dict):
        raise ValidationError("Target package manifest packages are missing")
    merged = copy.deepcopy(packages)
    for source in sorted(PACKAGE_ONLY_PACKAGE_FILES):
        group = PACKAGE_ONLY_PACKAGE_GROUPS.get(source)
        if not group:
            raise ValidationError(f"Package-only group is missing for {source}")
        path = require_file(resources / source, f"package-only file {source}")
        data = path.read_bytes()
        replacement: dict[str, object] = {
            "source": source,
            "target": f"/ext/{source}",
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "md5": hashlib.md5(data).hexdigest(),
        }

        found: list[tuple[str, int]] = []
        for existing_group, raw_entries in merged.items():
            if not isinstance(raw_entries, list):
                raise ValidationError(
                    f"Target package manifest group is invalid: {existing_group}"
                )
            for index, entry in enumerate(raw_entries):
                if (
                    isinstance(entry, dict)
                    and entry.get("target") == replacement["target"]
                ):
                    found.append((existing_group, index))
        if len(found) > 1:
            raise ValidationError(
                f"Package-only target is duplicated: {replacement['target']}"
            )
        if found and found[0][0] == group:
            merged[group][found[0][1]] = replacement
        else:
            if found:
                old_group, old_index = found[0]
                del merged[old_group][old_index]
            merged.setdefault(group, []).append(replacement)

    return merged


def build_package_release(
    repo_root: Path,
    build_dir: Path,
    out_dir: Path,
    package_id: str | None = None,
    target_release_tag: str | None = None,
    target_manifest: dict[str, object] | None = None,
    target_package_zip: Path | None = None,
    catalog_release_tag: str | None = None,
) -> dict[str, object]:
    firmware_json = json.loads(
        require_file(build_dir / "firmware.json", "firmware metadata").read_text(
            encoding="utf-8"
        )
    )
    if firmware_json["firmware_target"] != 7:
        raise ValidationError("firmware.json target is not Flipper Zero (7)")

    if target_manifest is not None:
        validate_target_release_id(target_manifest)

    commit, short, dirty = git_identity(repo_root)
    target_firmware = resolve_target_firmware(
        repo_root,
        firmware_json,
        target_manifest,
    )
    version = str(target_firmware["version"])
    catalog_identity = (
        catalog_release_identity(catalog_release_tag)
        if catalog_release_tag
        else None
    )
    if catalog_identity is not None:
        if target_release_tag is None:
            raise ValidationError(
                "Independent package catalog requires a target firmware release"
            )
        if target_release_tag.startswith("t-dev-"):
            target_channel = "dev"
        elif target_release_tag.startswith("v"):
            target_channel = "stable"
        else:
            raise ValidationError(
                "Independent package catalog target must be a stable or dev firmware release"
            )
        if catalog_identity[0] != target_channel:
            raise ValidationError(
                f"Catalog channel {catalog_identity[0]} does not match target "
                f"firmware channel {target_channel}"
            )
        baseline = catalog_baseline(repo_root, catalog_identity[0])
        if target_release_tag != baseline["release_tag"]:
            raise ValidationError(
                f"Catalog {catalog_identity[0]} baseline must be "
                f"{baseline['release_tag']}, got {target_release_tag}"
            )
        expected_firmware = {
            "version": baseline["firmware_version"],
            "api": baseline["api"],
            "target": baseline["target"],
        }
        for key, expected in expected_firmware.items():
            if target_firmware.get(key) != expected:
                raise ValidationError(
                    f"Catalog {catalog_identity[0]} baseline firmware {key} must be "
                    f"{expected!r}, got {target_firmware.get(key)!r}"
                )
    resolved_package_id = (
        package_id
        or catalog_release_tag
        or package_release_id(version, short, dirty)
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    if target_manifest is None:
        if target_package_zip is not None:
            raise ValidationError(
                "Target package ZIP cannot be used without a target manifest"
            )
        resources = require_file(
            build_dir / "resources/Manifest", "resource manifest"
        ).parent
        install_static_sd_resources(repo_root, resources)
        synced = sync_extapp_package_exports(build_dir, resources)
        validate_layout(resources)
        packages = package_entries(resources)
        manifest: dict[str, object] = {
            "schema": 2,
            "firmware": target_firmware,
            "artifacts": {},
            "packages": packages,
            "cleanup": release_cleanup_entries(),
        }
        package_resources = resources
        temporary_resources = None
    else:
        if target_package_zip is None:
            raise ValidationError(
                "Updating an existing release requires its package ZIP"
            )
        temporary_resources = tempfile.TemporaryDirectory(
            prefix="tumoflip-target-packages-"
        )
        package_resources = Path(temporary_resources.name) / "resources"
        package_resources.mkdir(parents=True)
        try:
            materialize_target_package(
                target_manifest,
                target_package_zip,
                package_resources,
            )
            synced = sync_extapp_package_exports(
                build_dir,
                package_resources,
                only_targets=PACKAGE_ONLY_PACKAGE_FILES,
            )
            synced_targets = {str(entry["target"]) for entry in synced}
            if synced_targets != PACKAGE_ONLY_PACKAGE_FILES:
                missing = sorted(PACKAGE_ONLY_PACKAGE_FILES - synced_targets)
                extra = sorted(synced_targets - PACKAGE_ONLY_PACKAGE_FILES)
                raise ValidationError(
                    f"Package-only build artifacts are incomplete; "
                    f"missing={missing}, extra={extra}"
                )
            packages = merge_package_only_entries(target_manifest, package_resources)
            manifest = copy.deepcopy(target_manifest)
            manifest.pop("release_id", None)
            manifest["firmware"] = target_firmware
            manifest["packages"] = packages
        except BaseException:
            temporary_resources.cleanup()
            raise

    manifest["package_release"] = {
        "type": "package-only",
        "id": resolved_package_id,
        "source_commit": commit,
        "source_dirty": dirty,
        "source_firmware_version": firmware_json["firmware_version"],
        "target_release_tag": target_release_tag,
        "target_release_id": (
            target_manifest.get("release_id") if target_manifest else None
        ),
        "firmware_flash_unchanged": True,
        "overlay_targets": sorted(PACKAGE_ONLY_PACKAGE_FILES)
        if target_manifest
        else [],
        "synced_extapps": synced,
    }
    if catalog_identity is not None:
        manifest["package_release"].update(
            {
                "catalog_channel": catalog_identity[0],
                "catalog_revision": catalog_identity[1],
                "catalog_release_tag": catalog_release_tag,
            }
        )
    manifest["release_id"] = manifest_release_id(manifest)

    try:
        manifest_path = out_dir / "tumoflip-packages.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        build_packages_zip(
            manifest,
            package_resources,
            out_dir / "tumoflip-packages.zip",
        )
    finally:
        if temporary_resources is not None:
            temporary_resources.cleanup()
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
    parser.add_argument("--target-manifest", type=Path)
    parser.add_argument("--target-package-zip", type=Path)
    parser.add_argument("--catalog-release-tag")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    build_dir = (repo_root / args.build_dir).resolve()
    try:
        firmware = json.loads(
            require_file(build_dir / "firmware.json", "firmware metadata").read_text(
                encoding="utf-8"
            )
        )
        target_manifest = None
        target_package_zip = None
        if args.target_manifest:
            if not args.target_release_tag:
                raise ValidationError(
                    "--target-manifest requires --target-release-tag"
                )
            if not args.target_package_zip:
                raise ValidationError(
                    "--target-manifest requires --target-package-zip"
                )
            target_manifest_path = (repo_root / args.target_manifest).resolve()
            target_manifest = json.loads(
                require_file(
                    target_manifest_path,
                    "target package manifest",
                ).read_text(encoding="utf-8")
            )
            target_package_zip = require_file(
                (repo_root / args.target_package_zip).resolve(),
                "target package ZIP",
            )
        elif args.target_package_zip:
            raise ValidationError(
                "--target-package-zip requires --target-manifest"
            )
        output_firmware = resolve_target_firmware(
            repo_root,
            firmware,
            target_manifest,
        )
        output_version = str(output_firmware["version"])
        out_dir = (
            (repo_root / args.out_dir).resolve()
            if args.out_dir
            else repo_root
            / "dist/f7-C"
            / f"f7-update-{output_version}"
        )
        manifest = build_package_release(
            repo_root,
            build_dir,
            out_dir,
            package_id=args.package_id,
            target_release_tag=args.target_release_tag,
            target_manifest=target_manifest,
            target_package_zip=target_package_zip,
            catalog_release_tag=args.catalog_release_tag,
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
