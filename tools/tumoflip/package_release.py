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
        PACKAGE_RELEASE_OVERLAY_FILES,
        PACKAGE_RELEASE_OVERLAY_GROUPS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        package_extapp_exports,
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
        PACKAGE_RELEASE_OVERLAY_FILES,
        PACKAGE_RELEASE_OVERLAY_GROUPS,
        ValidationError,
        api_version,
        install_static_sd_resources,
        manifest_release_id,
        package_entries,
        package_extapp_exports,
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
CATALOG_LINEAGE_PATH = Path("tools/tumoflip/package_catalog_lineage.json")

# Selective catalog deltas are intentionally keyed by a small audited name rather
# than accepting an arbitrary path from workflow_dispatch.  Adding another app is
# therefore a source-reviewed change, not a release-operator typo.
SELECTIVE_CATALOG_OVERLAYS = {
    "esp_flasher": {
        "source": "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
        "fbt_target": "fap_esp_flasher",
    },
}


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


def selective_catalog_overlay_names(value: str) -> tuple[str, ...]:
    """Normalize an untrusted workflow value to sorted audited ASCII names."""
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        raise ValidationError("Selective catalog overlays contain control characters")
    names = value.split(",")
    if not names or any(not name or name != name.strip() for name in names):
        raise ValidationError("Selective catalog overlays contain an empty name")
    if len(names) != len(set(names)):
        raise ValidationError("Selective catalog overlays contain duplicates")
    unknown = sorted(set(names) - set(SELECTIVE_CATALOG_OVERLAYS))
    if unknown:
        raise ValidationError(f"Selective catalog overlays are not allowlisted: {unknown}")
    return tuple(sorted(names))


def selective_catalog_overlay_sources(value: str) -> frozenset[str]:
    """Resolve a comma-separated audited overlay list, rejecting ambiguity."""
    names = selective_catalog_overlay_names(value)
    return frozenset(
        str(SELECTIVE_CATALOG_OVERLAYS[name]["source"]) for name in names
    )


def selective_catalog_build_targets(value: str) -> tuple[str, ...]:
    """Return the exact FBT targets for a validated selective overlay list."""
    sources = selective_catalog_overlay_sources(value)
    return tuple(
        str(spec["fbt_target"])
        for spec in SELECTIVE_CATALOG_OVERLAYS.values()
        if spec["source"] in sources
    )


def copy_selective_extapp_exports(
    build_dir: Path,
    resources: Path,
    selected_sources: frozenset[str],
) -> list[dict[str, object]]:
    """Copy only selected build outputs without pruning or touching other files."""
    exports: dict[str, list[str]] = {}
    for filename, source in package_extapp_exports().items():
        if source in selected_sources:
            exports.setdefault(source, []).append(filename)

    synced: list[dict[str, object]] = []
    for source in sorted(selected_sources):
        candidates = [
            build_dir / ".extapps" / filename
            for filename in exports.get(source, [])
            if (build_dir / ".extapps" / filename).is_file()
        ]
        if len(candidates) != 1:
            raise ValidationError(
                f"Selective catalog requires exactly one build artifact for {source}; "
                f"found={len(candidates)}"
            )
        output = resources / source
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(candidates[0].read_bytes())
        synced.append(
            {
                "source": candidates[0].relative_to(build_dir).as_posix(),
                "target": source,
                "bytes": output.stat().st_size,
                "sha256": file_sha256(output),
                "md5": hashlib.md5(output.read_bytes()).hexdigest(),
            }
        )
    return synced


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


def catalog_lineage_base(
    repo_root: Path,
    channel: str,
) -> dict[str, str]:
    """Return the one immutable catalog authorized as a selective delta base."""
    path = require_file(
        repo_root / CATALOG_LINEAGE_PATH,
        "package catalog lineage",
    )
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValidationError(f"Package catalog lineage is invalid: {error}") from error
    if document.get("schema") != 1:
        raise ValidationError("Package catalog lineage must use schema 1")
    entry = document.get(channel)
    if not isinstance(entry, dict):
        raise ValidationError(f"Package catalog lineage is missing for {channel}")
    required = {
        "firmware_version": None,
        "manifest_sha256": 64,
        "package_zip_sha256": 64,
        "release_id": 64,
        "release_tag": None,
        "source_commit": 40,
        "target_release_id": 64,
        "target_release_tag": None,
        "target_source_commit": 40,
    }
    validated: dict[str, str] = {}
    for key, hex_length in required.items():
        value = entry.get(key)
        if not isinstance(value, str) or not value:
            raise ValidationError(f"Package catalog lineage {channel}.{key} is invalid")
        if hex_length is not None and not _is_lower_hex(value, hex_length):
            raise ValidationError(f"Package catalog lineage {channel}.{key} is invalid")
        validated[key] = value
    if catalog_release_identity(validated["release_tag"])[0] != channel:
        raise ValidationError(f"Package catalog lineage channel differs for {channel}")
    return validated


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


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
    for source in sorted(PACKAGE_RELEASE_OVERLAY_FILES):
        group = PACKAGE_RELEASE_OVERLAY_GROUPS.get(source)
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


def _catalog_release(
    manifest: dict[str, object],
    *,
    expected_channel: str,
) -> dict[str, object]:
    release = manifest.get("package_release")
    if not isinstance(release, dict) or release.get("type") != "package-only":
        raise ValidationError("Compatible manifest is not a package-only release")
    tag = release.get("catalog_release_tag")
    channel = release.get("catalog_channel")
    revision = release.get("catalog_revision")
    if (
        not isinstance(tag, str)
        or catalog_release_identity(tag) != (channel, revision)
        or channel != expected_channel
    ):
        raise ValidationError("Compatible catalog identity is invalid")
    if release.get("id") != tag or release.get("source_dirty") is not False:
        raise ValidationError("Compatible catalog provenance is invalid")
    source_commit = release.get("source_commit")
    if (
        not isinstance(source_commit, str)
        or len(source_commit) != 40
        or any(character not in "0123456789abcdef" for character in source_commit)
    ):
        raise ValidationError("Compatible catalog source commit is invalid")
    overlays = release.get("overlay_targets")
    if (
        not isinstance(overlays, list)
        or not overlays
        or len(overlays) != len(set(overlays))
        or not all(isinstance(source, str) and source for source in overlays)
    ):
        raise ValidationError("Compatible catalog overlay targets are invalid")
    if release.get("compatible_releases") not in (None, []):
        raise ValidationError("Nested compatible catalogs are not supported")
    return release


def build_catalog_reconciliation(
    repo_root: Path,
    out_dir: Path,
    *,
    target_release_tag: str,
    target_source_commit: str,
    target_manifest: dict[str, object],
    target_package_zip: Path,
    compatible_manifest: dict[str, object],
    compatible_manifest_sha256: str,
    compatible_package_zip: Path,
    compatible_release_tag: str,
    catalog_release_tag: str,
) -> dict[str, object]:
    """Move a catalog to an accepted firmware baseline without false mass updates.

    The new ZIP remains the accepted firmware release's exact package set. Only the
    previous catalog's explicit overlays may be admitted as equivalent builds, and
    only when that catalog was built from the exact accepted firmware source commit.
    Unknown, inherited baseline, or nested aliases remain fail-closed.
    """
    validate_target_release_id(target_manifest)
    validate_target_release_id(compatible_manifest)
    channel, revision = catalog_release_identity(catalog_release_tag)
    if revision < 2:
        raise ValidationError("A reconciled catalog requires revision 002 or newer")
    if (
        len(target_source_commit) != 40
        or any(character not in "0123456789abcdef" for character in target_source_commit)
    ):
        raise ValidationError("Target firmware source commit is invalid")
    if (
        len(compatible_manifest_sha256) != 64
        or any(
            character not in "0123456789abcdef"
            for character in compatible_manifest_sha256
        )
    ):
        raise ValidationError("Compatible manifest SHA-256 is invalid")

    if channel == "dev":
        expected_prefix = "t-dev-"
    else:
        expected_prefix = "v"
    if not target_release_tag.startswith(expected_prefix):
        raise ValidationError("Catalog channel does not match target firmware release")
    baseline = catalog_baseline(repo_root, channel)
    if target_release_tag != baseline["release_tag"]:
        raise ValidationError(
            f"Catalog {channel} baseline must be {baseline['release_tag']}, "
            f"got {target_release_tag}"
        )

    target_firmware = target_manifest.get("firmware")
    compatible_firmware = compatible_manifest.get("firmware")
    if not isinstance(target_firmware, dict) or not isinstance(compatible_firmware, dict):
        raise ValidationError("Catalog firmware identity is missing")
    for key, expected in {
        "name": "tumoflip",
        "version": baseline["firmware_version"],
        "api": baseline["api"],
        "target": baseline["target"],
    }.items():
        if target_firmware.get(key) != expected:
            raise ValidationError(
                f"Catalog {channel} baseline firmware {key} must be {expected!r}"
            )
    for key in ("name", "api", "target"):
        if compatible_firmware.get(key) != target_firmware.get(key):
            raise ValidationError(f"Compatible firmware {key} differs")

    compatible_release = _catalog_release(
        compatible_manifest,
        expected_channel=channel,
    )
    if compatible_release["catalog_release_tag"] != compatible_release_tag:
        raise ValidationError("Compatible catalog tag differs from its manifest")
    compatible_revision = compatible_release["catalog_revision"]
    if not isinstance(compatible_revision, int) or compatible_revision >= revision:
        raise ValidationError("Compatible catalog revision must precede the new revision")
    if compatible_release["source_commit"] != target_source_commit:
        raise ValidationError(
            "Compatible overlays were not built from the accepted firmware source commit"
        )
    if compatible_release.get("source_firmware_version") != target_firmware["version"]:
        raise ValidationError(
            "Compatible catalog source firmware differs from the accepted baseline"
        )

    target_entries = _validated_target_entries(target_manifest)
    compatible_entries = _validated_target_entries(compatible_manifest)
    overlay_sources = set(compatible_release["overlay_targets"])
    if not overlay_sources <= set(target_entries) or not overlay_sources <= set(
        compatible_entries
    ):
        raise ValidationError("Compatible catalog overlay target is missing")
    for entries in (target_entries, compatible_entries):
        for entry in entries.values():
            if entry.get("compatible_builds") not in (None, []):
                raise ValidationError("Nested compatible package builds are not supported")

    target_groups = {
        entry["source"]: group
        for group, entries in target_manifest["packages"].items()
        for entry in entries
    }
    compatible_groups = {
        entry["source"]: group
        for group, entries in compatible_manifest["packages"].items()
        for entry in entries
    }
    for source in overlay_sources:
        if target_groups[source] != compatible_groups[source]:
            raise ValidationError(f"Compatible overlay group differs for {source}")

    target_resources_context = tempfile.TemporaryDirectory(
        prefix="tumoflip-reconciled-target-"
    )
    compatible_resources_context = tempfile.TemporaryDirectory(
        prefix="tumoflip-reconciled-compatible-"
    )
    target_resources = Path(target_resources_context.name) / "resources"
    compatible_resources = Path(compatible_resources_context.name) / "resources"
    target_resources.mkdir(parents=True)
    compatible_resources.mkdir(parents=True)
    try:
        materialize_target_package(target_manifest, target_package_zip, target_resources)
        materialize_target_package(
            compatible_manifest,
            compatible_package_zip,
            compatible_resources,
        )

        manifest = copy.deepcopy(target_manifest)
        manifest.pop("release_id", None)
        packages = manifest.get("packages")
        if not isinstance(packages, dict):
            raise ValidationError("Target package manifest packages are missing")
        aliases = 0
        compatible_release_id = compatible_manifest["release_id"]
        for entries in packages.values():
            for entry in entries:
                source = entry["source"]
                entry.pop("compatible_builds", None)
                if source not in overlay_sources:
                    continue
                old = compatible_entries[source]
                if old["md5"] == entry["md5"]:
                    continue
                entry["compatible_builds"] = [
                    {
                        "bytes": old["bytes"],
                        "sha256": old["sha256"],
                        "md5": old["md5"],
                        "release_id": compatible_release_id,
                    }
                ]
                aliases += 1
        if aliases == 0:
            raise ValidationError("Compatible catalog has no distinct overlay builds")

        commit, _short, dirty = git_identity(repo_root)
        if dirty:
            raise ValidationError("Reconciled catalog source worktree must be clean")
        manifest["package_release"] = {
            "type": "package-only",
            "id": catalog_release_tag,
            "source_commit": commit,
            "source_dirty": False,
            "source_firmware_version": target_firmware["version"],
            "target_release_tag": target_release_tag,
            "target_release_id": target_manifest["release_id"],
            "firmware_flash_unchanged": True,
            "overlay_targets": [],
            "synced_extapps": [],
            "catalog_channel": channel,
            "catalog_revision": revision,
            "catalog_release_tag": catalog_release_tag,
            "compatible_releases": [
                {
                    "release_tag": compatible_release["catalog_release_tag"],
                    "release_id": compatible_release_id,
                    "manifest_sha256": compatible_manifest_sha256,
                    "source_commit": compatible_release["source_commit"],
                }
            ],
        }
        manifest["release_id"] = manifest_release_id(manifest)
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "tumoflip-packages.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        build_packages_zip(
            manifest,
            target_resources,
            out_dir / "tumoflip-packages.zip",
        )
        if file_sha256(out_dir / "tumoflip-packages.zip") != file_sha256(
            target_package_zip
        ):
            raise ValidationError("Reconciled package ZIP differs from target firmware ZIP")
        return manifest
    finally:
        target_resources_context.cleanup()
        compatible_resources_context.cleanup()


def _is_lower_hex(value: object, length: int) -> bool:
    return (
        isinstance(value, str)
        and len(value) == length
        and all(character in "0123456789abcdef" for character in value)
    )


def _selective_base_release(
    manifest: dict[str, object],
    *,
    expected_channel: str,
) -> dict[str, object]:
    """Validate provenance carried by an immutable catalog used as a delta base."""
    release = manifest.get("package_release")
    if not isinstance(release, dict) or release.get("type") != "package-only":
        raise ValidationError("Base manifest is not a package-only release")
    tag = release.get("catalog_release_tag")
    channel = release.get("catalog_channel")
    revision = release.get("catalog_revision")
    if (
        not isinstance(tag, str)
        or catalog_release_identity(tag) != (channel, revision)
        or channel != expected_channel
    ):
        raise ValidationError("Base catalog identity is invalid")
    if release.get("id") != tag or release.get("source_dirty") is not False:
        raise ValidationError("Base catalog provenance is invalid")
    if not _is_lower_hex(release.get("source_commit"), 40):
        raise ValidationError("Base catalog source commit is invalid")

    overlays = release.get("overlay_targets")
    if (
        not isinstance(overlays, list)
        or len(overlays) != len(set(overlays))
        or not all(isinstance(source, str) and source for source in overlays)
        or not set(overlays) <= PACKAGE_RELEASE_OVERLAY_FILES
    ):
        raise ValidationError("Base catalog overlay targets are invalid")
    modified = release.get("catalog_modified_targets", [])
    if (
        not isinstance(modified, list)
        or len(modified) != len(set(modified))
        or not all(isinstance(source, str) and source for source in modified)
        or not set(modified) <= PACKAGE_RELEASE_OVERLAY_FILES
    ):
        raise ValidationError("Base catalog modified targets are invalid")

    compatible = release.get("compatible_releases", [])
    if not isinstance(compatible, list):
        raise ValidationError("Base catalog compatible lineage is invalid")
    compatible_ids: set[str] = set()
    compatible_tags: set[str] = set()
    for item in compatible:
        if not isinstance(item, dict):
            raise ValidationError("Base catalog compatible lineage is invalid")
        item_tag = item.get("release_tag")
        item_release_id = item.get("release_id")
        if not isinstance(item_tag, str):
            raise ValidationError("Base catalog compatible tag is invalid")
        item_channel, item_revision = catalog_release_identity(item_tag)
        if (
            item_channel != channel
            or not isinstance(revision, int)
            or item_revision >= revision
        ):
            raise ValidationError("Base catalog compatible revision is invalid")
        if (
            item_tag in compatible_tags
            or not _is_lower_hex(item_release_id, 64)
            or item_release_id in compatible_ids
            or not _is_lower_hex(item.get("manifest_sha256"), 64)
            or not _is_lower_hex(item.get("source_commit"), 40)
        ):
            raise ValidationError("Base catalog compatible provenance is invalid")
        compatible_tags.add(item_tag)
        compatible_ids.add(str(item_release_id))

    entries = _validated_target_entries(manifest)
    referenced_compatible_ids: set[str] = set()
    for source, entry in entries.items():
        aliases = entry.get("compatible_builds")
        if aliases is None:
            continue
        if not isinstance(aliases, list) or not aliases:
            raise ValidationError(f"Base compatible builds are invalid for {source}")
        seen_aliases: set[tuple[object, ...]] = set()
        for alias in aliases:
            if not isinstance(alias, dict):
                raise ValidationError(f"Base compatible build is invalid for {source}")
            identity = (
                alias.get("bytes"),
                alias.get("sha256"),
                alias.get("md5"),
                alias.get("release_id"),
            )
            if (
                identity in seen_aliases
                or not isinstance(alias.get("bytes"), int)
                or isinstance(alias.get("bytes"), bool)
                or not 0 < int(alias["bytes"]) <= MAX_TARGET_PACKAGE_FILE_BYTES
                or not _is_lower_hex(alias.get("sha256"), 64)
                or not _is_lower_hex(alias.get("md5"), 32)
                or alias.get("release_id") not in compatible_ids
            ):
                raise ValidationError(f"Base compatible build is invalid for {source}")
            seen_aliases.add(identity)
            referenced_compatible_ids.add(str(alias["release_id"]))
    if referenced_compatible_ids != compatible_ids:
        raise ValidationError(
            "Base compatible build references differ from compatible releases"
        )
    return release


def build_selective_catalog_release(
    repo_root: Path,
    build_dir: Path,
    out_dir: Path,
    *,
    target_release_tag: str,
    target_source_commit: str,
    target_manifest: dict[str, object],
    target_package_zip: Path,
    base_manifest: dict[str, object],
    base_manifest_sha256: str,
    base_package_zip: Path,
    base_release_tag: str,
    base_source_commit: str,
    catalog_release_tag: str,
    overlay_sources: frozenset[str],
) -> dict[str, object]:
    """Publish a minimal catalog delta over one exact immutable predecessor.

    The prior catalog is authoritative.  Only explicitly allowlisted entries are
    rebuilt; every other manifest entry and ZIP payload is retained exactly.
    """
    validate_target_release_id(target_manifest)
    validate_target_release_id(base_manifest)
    channel, revision = catalog_release_identity(catalog_release_tag)
    base_release = _selective_base_release(
        base_manifest,
        expected_channel=channel,
    )
    lineage = catalog_lineage_base(repo_root, channel)
    if base_release.get("catalog_release_tag") != base_release_tag:
        raise ValidationError("Base catalog tag differs from its manifest")
    if base_release_tag != lineage["release_tag"]:
        raise ValidationError("Base catalog is not the pinned latest lineage entry")
    base_revision = base_release.get("catalog_revision")
    if not isinstance(base_revision, int) or base_revision >= revision:
        raise ValidationError("Base catalog must precede the new revision")
    if base_release.get("source_commit") != base_source_commit:
        raise ValidationError("Base catalog tag commit differs from its manifest")
    if not _is_lower_hex(base_manifest_sha256, 64):
        raise ValidationError("Base manifest SHA-256 is invalid")
    if not _is_lower_hex(target_source_commit, 40):
        raise ValidationError("Target firmware source commit is invalid")
    package_zip_sha256 = file_sha256(base_package_zip)
    pinned_values = {
        "firmware_version": str(base_manifest.get("firmware", {}).get("version")),
        "manifest_sha256": base_manifest_sha256,
        "package_zip_sha256": package_zip_sha256,
        "release_id": str(base_manifest["release_id"]),
        "source_commit": base_source_commit,
        "target_release_id": str(base_release.get("target_release_id")),
        "target_release_tag": str(base_release.get("target_release_tag")),
        "target_source_commit": target_source_commit,
    }
    for key, actual in pinned_values.items():
        if actual != lineage[key]:
            raise ValidationError(f"Base catalog pinned {key} differs")

    if not overlay_sources:
        raise ValidationError("Selective catalog overlay list is empty")
    allowed_sources = {
        str(spec["source"]) for spec in SELECTIVE_CATALOG_OVERLAYS.values()
    }
    if not overlay_sources <= allowed_sources:
        raise ValidationError(
            f"Selective catalog targets are not allowlisted: "
            f"{sorted(overlay_sources - allowed_sources)}"
        )

    baseline = catalog_baseline(repo_root, channel)
    if target_release_tag != baseline["release_tag"]:
        raise ValidationError(
            f"Catalog {channel} baseline must be {baseline['release_tag']}, "
            f"got {target_release_tag}"
        )
    target_firmware = target_manifest.get("firmware")
    base_firmware = base_manifest.get("firmware")
    expected_firmware = {
        "name": "tumoflip",
        "version": baseline["firmware_version"],
        "api": baseline["api"],
        "target": baseline["target"],
    }
    if not isinstance(target_firmware, dict) or not isinstance(base_firmware, dict):
        raise ValidationError("Selective catalog firmware identity is missing")
    for key, expected in expected_firmware.items():
        if target_firmware.get(key) != expected:
            raise ValidationError(
                f"Catalog {channel} baseline firmware {key} must be {expected!r}"
            )
    if base_firmware != target_firmware:
        raise ValidationError("Base catalog firmware identity differs from target")
    if (
        base_release.get("target_release_tag") != target_release_tag
        or base_release.get("target_release_id") != target_manifest["release_id"]
        or base_release.get("source_firmware_version") != target_firmware["version"]
    ):
        raise ValidationError("Base catalog target lineage differs from target firmware")
    for key in ("schema", "safety", "artifacts", "cleanup"):
        if base_manifest.get(key) != target_manifest.get(key):
            raise ValidationError(f"Base catalog {key} differs from target firmware")

    target_entries = _validated_target_entries(target_manifest)
    base_entries = _validated_target_entries(base_manifest)
    target_groups = {
        entry["source"]: group
        for group, entries in target_manifest["packages"].items()
        for entry in entries
    }
    base_groups = {
        entry["source"]: group
        for group, entries in base_manifest["packages"].items()
        for entry in entries
    }
    if set(base_entries) != set(target_entries) or base_groups != target_groups:
        raise ValidationError("Base catalog package topology differs from target firmware")
    modified_sources = set(base_release.get("catalog_modified_targets", []))
    for source, base_entry in base_entries.items():
        canonical_base = dict(base_entry)
        canonical_base.pop("compatible_builds", None)
        if canonical_base != target_entries[source] and source not in modified_sources:
            raise ValidationError(f"Base catalog has untracked modified target: {source}")

    commit, _short, dirty = git_identity(repo_root)
    if dirty:
        raise ValidationError("Selective catalog source worktree must be clean")

    base_resources_context = tempfile.TemporaryDirectory(
        prefix="tumoflip-selective-base-"
    )
    base_resources = Path(base_resources_context.name) / "resources"
    base_resources.mkdir(parents=True)
    try:
        materialize_target_package(base_manifest, base_package_zip, base_resources)
        with tempfile.TemporaryDirectory(
            prefix="tumoflip-selective-target-"
        ) as target_dir:
            target_resources = Path(target_dir) / "resources"
            target_resources.mkdir(parents=True)
            materialize_target_package(
                target_manifest,
                target_package_zip,
                target_resources,
            )
        synced = copy_selective_extapp_exports(
            build_dir,
            base_resources,
            overlay_sources,
        )
        synced_targets = {str(entry["target"]) for entry in synced}
        if synced_targets != overlay_sources:
            missing = sorted(overlay_sources - synced_targets)
            extra = sorted(synced_targets - overlay_sources)
            raise ValidationError(
                f"Selective catalog build artifacts are incomplete; "
                f"missing={missing}, extra={extra}"
            )

        manifest = copy.deepcopy(base_manifest)
        manifest.pop("release_id", None)
        packages = manifest.get("packages")
        if not isinstance(packages, dict):
            raise ValidationError("Base catalog packages are missing")
        for source in sorted(overlay_sources):
            expected_group = PACKAGE_RELEASE_OVERLAY_GROUPS.get(source)
            if expected_group is None or base_groups[source] != expected_group:
                raise ValidationError(f"Selective catalog group is invalid for {source}")
            data = require_file(
                base_resources / source,
                f"selective catalog file {source}",
            ).read_bytes()
            replacement = {
                "source": source,
                "target": f"/ext/{source}",
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "md5": hashlib.md5(data).hexdigest(),
            }
            old_entry = base_entries[source]
            if all(
                replacement[key] == old_entry.get(key)
                for key in ("bytes", "sha256", "md5")
            ):
                raise ValidationError(
                    f"Selective catalog target is unchanged: {source}"
                )
            matches = [
                index
                for index, entry in enumerate(packages[expected_group])
                if isinstance(entry, dict) and entry.get("source") == source
            ]
            if len(matches) != 1:
                raise ValidationError(f"Selective catalog target is missing: {source}")
            packages[expected_group][matches[0]] = replacement

        remaining_compatible_ids = {
            str(alias["release_id"])
            for entries in packages.values()
            for entry in entries
            for alias in entry.get("compatible_builds", [])
        }
        compatible_releases = [
            copy.deepcopy(item)
            for item in base_release.get("compatible_releases", [])
            if item["release_id"] in remaining_compatible_ids
        ]
        if {str(item["release_id"]) for item in compatible_releases} != (
            remaining_compatible_ids
        ):
            raise ValidationError(
                "Selective catalog compatible references cannot be resolved"
            )

        manifest["package_release"] = {
            "type": "package-only",
            "id": catalog_release_tag,
            "source_commit": commit,
            "source_dirty": False,
            "source_firmware_version": target_firmware["version"],
            "target_release_tag": target_release_tag,
            "target_release_id": target_manifest["release_id"],
            "target_source_commit": target_source_commit,
            "firmware_flash_unchanged": True,
            "overlay_targets": sorted(overlay_sources),
            "catalog_modified_targets": sorted(modified_sources | overlay_sources),
            "synced_extapps": synced,
            "catalog_channel": channel,
            "catalog_revision": revision,
            "catalog_release_tag": catalog_release_tag,
            "base_catalog": {
                "release_tag": base_release_tag,
                "release_id": base_manifest["release_id"],
                "manifest_sha256": base_manifest_sha256,
                "package_zip_sha256": package_zip_sha256,
                "source_commit": base_source_commit,
            },
            "compatible_releases": compatible_releases,
        }
        manifest["release_id"] = manifest_release_id(manifest)
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "tumoflip-packages.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        output_zip = out_dir / "tumoflip-packages.zip"
        build_packages_zip(manifest, base_resources, output_zip)

        output_entries = _validated_target_entries(manifest)
        for source in set(base_entries) - overlay_sources:
            if output_entries[source] != base_entries[source]:
                raise ValidationError(
                    f"Selective catalog changed non-selected manifest entry: {source}"
                )
        with zipfile.ZipFile(base_package_zip) as old_zip, zipfile.ZipFile(
            output_zip
        ) as new_zip:
            if old_zip.namelist() != new_zip.namelist():
                raise ValidationError("Selective catalog ZIP topology changed")
            for source in set(base_entries) - overlay_sources:
                if old_zip.read(source) != new_zip.read(source):
                    raise ValidationError(
                        f"Selective catalog changed non-selected ZIP payload: {source}"
                    )
        return manifest
    finally:
        base_resources_context.cleanup()


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
                only_targets=PACKAGE_RELEASE_OVERLAY_FILES,
            )
            synced_targets = {str(entry["target"]) for entry in synced}
            if synced_targets != PACKAGE_RELEASE_OVERLAY_FILES:
                missing = sorted(PACKAGE_RELEASE_OVERLAY_FILES - synced_targets)
                extra = sorted(synced_targets - PACKAGE_RELEASE_OVERLAY_FILES)
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
        "overlay_targets": sorted(PACKAGE_RELEASE_OVERLAY_FILES)
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
    parser.add_argument("--target-source-commit")
    parser.add_argument("--compatible-manifest", type=Path)
    parser.add_argument("--compatible-package-zip", type=Path)
    parser.add_argument("--compatible-release-tag")
    parser.add_argument("--base-manifest", type=Path)
    parser.add_argument("--base-package-zip", type=Path)
    parser.add_argument("--base-release-tag")
    parser.add_argument("--base-source-commit")
    parser.add_argument(
        "--selective-overlays",
        help="Comma-separated audited overlay names, for example esp_flasher",
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    build_dir = (repo_root / args.build_dir).resolve()
    try:
        target_manifest = None
        target_package_zip = None
        target_manifest_path = None
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
        reconciliation_requested = bool(
            args.compatible_manifest
            or args.compatible_package_zip
            or args.compatible_release_tag
        )
        selective_requested = bool(
            args.base_manifest
            or args.base_package_zip
            or args.base_release_tag
            or args.base_source_commit
            or args.selective_overlays
        )
        if reconciliation_requested and selective_requested:
            raise ValidationError(
                "Catalog reconciliation and selective overlay are mutually exclusive"
            )
        if reconciliation_requested or selective_requested:
            if target_manifest is None:
                raise ValidationError(
                    "Catalog update requires a target manifest"
                )
            output_firmware = target_manifest.get("firmware")
            if not isinstance(output_firmware, dict):
                raise ValidationError("Target package manifest firmware is missing")
        else:
            firmware = json.loads(
                require_file(
                    build_dir / "firmware.json", "firmware metadata"
                ).read_text(encoding="utf-8")
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
        if selective_requested:
            if not (
                args.base_manifest
                and args.base_package_zip
                and args.base_release_tag
                and args.base_source_commit
                and args.selective_overlays
                and args.catalog_release_tag
                and args.target_source_commit
                and target_manifest is not None
                and target_package_zip is not None
            ):
                raise ValidationError(
                    "Selective catalog release requires target and base assets, "
                    "catalog tags, source commits, and selective overlays"
                )
            base_manifest_path = require_file(
                (repo_root / args.base_manifest).resolve(),
                "base package manifest",
            )
            base_manifest = json.loads(
                base_manifest_path.read_text(encoding="utf-8")
            )
            base_package_zip = require_file(
                (repo_root / args.base_package_zip).resolve(),
                "base package ZIP",
            )
            manifest = build_selective_catalog_release(
                repo_root,
                build_dir,
                out_dir,
                target_release_tag=args.target_release_tag,
                target_source_commit=args.target_source_commit,
                target_manifest=target_manifest,
                target_package_zip=target_package_zip,
                base_manifest=base_manifest,
                base_manifest_sha256=file_sha256(base_manifest_path),
                base_package_zip=base_package_zip,
                base_release_tag=args.base_release_tag,
                base_source_commit=args.base_source_commit,
                catalog_release_tag=args.catalog_release_tag,
                overlay_sources=selective_catalog_overlay_sources(
                    args.selective_overlays
                ),
            )
        elif reconciliation_requested:
            if not (
                args.compatible_manifest
                and args.compatible_package_zip
                and args.compatible_release_tag
                and args.catalog_release_tag
                and args.target_source_commit
                and target_manifest is not None
                and target_manifest_path is not None
                and target_package_zip is not None
            ):
                raise ValidationError(
                    "Catalog reconciliation requires target and compatible assets, "
                    "catalog tag, and target source commit"
                )
            compatible_manifest_path = require_file(
                (repo_root / args.compatible_manifest).resolve(),
                "compatible package manifest",
            )
            compatible_manifest = json.loads(
                compatible_manifest_path.read_text(encoding="utf-8")
            )
            compatible_package_zip = require_file(
                (repo_root / args.compatible_package_zip).resolve(),
                "compatible package ZIP",
            )
            manifest = build_catalog_reconciliation(
                repo_root,
                out_dir,
                target_release_tag=args.target_release_tag,
                target_source_commit=args.target_source_commit,
                target_manifest=target_manifest,
                target_package_zip=target_package_zip,
                compatible_manifest=compatible_manifest,
                compatible_manifest_sha256=file_sha256(compatible_manifest_path),
                compatible_package_zip=compatible_package_zip,
                compatible_release_tag=args.compatible_release_tag,
                catalog_release_tag=args.catalog_release_tag,
            )
        else:
            if args.target_source_commit:
                raise ValidationError(
                    "--target-source-commit requires a compatible or base catalog"
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
