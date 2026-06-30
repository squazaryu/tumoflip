#!/usr/bin/env python3
"""Atomically apply Tumoflip package groups to a mounted Flipper SD card."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import uuid
from pathlib import Path, PurePosixPath

try:
    from .validate_release import manifest_release_id
except ImportError:
    from validate_release import manifest_release_id


class PackageError(RuntimeError):
    pass


PACKAGE_STATE_FILE = "package-state.txt"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, object]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 2:
        raise PackageError(f"Unsupported package schema: {manifest.get('schema')}")
    expected = manifest.get("release_id")
    payload = dict(manifest)
    payload.pop("release_id", None)
    actual = manifest_release_id(payload)
    if expected != actual:
        raise PackageError("Package release_id does not match manifest contents")
    return manifest


def ext_relative(path: str) -> Path:
    pure = PurePosixPath(path)
    if not pure.is_absolute() or len(pure.parts) < 3 or pure.parts[1] != "ext":
        raise PackageError(f"Package target is outside /ext: {path}")
    relative = Path(*pure.parts[2:])
    if ".." in relative.parts:
        raise PackageError(f"Package target contains traversal: {path}")
    return relative


def safe_source(resources_root: Path, source: str) -> Path:
    root = resources_root.resolve()
    path = (root / source).resolve()
    if not path.is_relative_to(root):
        raise PackageError(f"Package source escapes resources root: {source}")
    return path


def selected_entries(
    manifest: dict[str, object], groups: set[str] | None
) -> list[tuple[str, dict[str, object]]]:
    packages = manifest.get("packages")
    if not isinstance(packages, dict):
        raise PackageError("Manifest packages must be an object")
    selected = set(packages) if groups is None else groups
    unknown = selected - set(packages)
    if unknown:
        raise PackageError(f"Unknown package groups: {sorted(unknown)}")
    return [(group, entry) for group in sorted(selected) for entry in packages[group]]


def verify_sources(
    resources_root: Path, entries: list[tuple[str, dict[str, object]]]
) -> None:
    for _, entry in entries:
        source = safe_source(resources_root, str(entry["source"]))
        if not source.is_file():
            raise PackageError(f"Package source is missing: {source}")
        if source.stat().st_size != int(entry["bytes"]):
            raise PackageError(f"Package source size mismatch: {source}")
        if sha256(source) != entry["sha256"]:
            raise PackageError(f"Package source SHA-256 mismatch: {source}")
        ext_relative(str(entry["target"]))


def package_state_text(
    manifest: dict[str, object],
    transaction: str,
    selected_groups: list[str],
    installed_count: int,
    cleanup_candidates: int,
) -> str:
    firmware = manifest.get("firmware", {})
    package_release = manifest.get("package_release", {})
    firmware_version = "unknown"
    firmware_api = "unknown"
    if isinstance(firmware, dict):
        firmware_version = str(firmware.get("version", firmware_version))
        firmware_api = str(firmware.get("api", firmware_api))

    package_release_id = "firmware"
    if isinstance(package_release, dict):
        package_release_id = str(package_release.get("id", package_release_id))

    return "\n".join(
        (
            "Filetype: Tumoflip Package State",
            "Version: 1",
            "Schema: 2",
            f"ReleaseId: {manifest['release_id']}",
            f"Transaction: {transaction}",
            f"Firmware: {firmware_version}",
            f"FirmwareApi: {firmware_api}",
            f"PackageRelease: {package_release_id}",
            f"Groups: {','.join(selected_groups)}",
            f"InstalledFiles: {installed_count}",
            f"CleanupCandidates: {cleanup_candidates}",
            f"Rollback: /.tumoflip/rollback/{transaction}",
            "",
        )
    )


def apply_packages(
    manifest_path: Path,
    resources_root: Path,
    sd_root: Path,
    groups: set[str] | None = None,
    dry_run: bool = False,
) -> dict[str, object]:
    manifest = load_manifest(manifest_path)
    entries = selected_entries(manifest, groups)
    verify_sources(resources_root, entries)
    if dry_run:
        return {"release_id": manifest["release_id"], "verified": len(entries)}

    if not sd_root.is_dir():
        raise PackageError(f"SD root is not a directory: {sd_root}")

    transaction = f"{str(manifest['release_id'])[:16]}-{uuid.uuid4().hex[:8]}"
    metadata_root = sd_root / ".tumoflip"
    selected_groups = sorted(manifest["packages"] if groups is None else groups)
    staging_root = metadata_root / "staging" / transaction
    rollback_root = metadata_root / "rollback" / transaction
    staging_root.mkdir(parents=True)
    rollback_root.mkdir(parents=True)

    try:
        for _, entry in entries:
            relative = ext_relative(str(entry["target"]))
            staged = staging_root / relative
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(safe_source(resources_root, str(entry["source"])), staged)
            if sha256(staged) != entry["sha256"]:
                raise PackageError(f"Staged SHA-256 mismatch: {staged}")
    except Exception:
        shutil.rmtree(staging_root, ignore_errors=True)
        shutil.rmtree(rollback_root, ignore_errors=True)
        raise

    installed: list[tuple[Path, Path | None]] = []
    removed_legacy: list[tuple[Path, Path]] = []
    try:
        for _, entry in entries:
            relative = ext_relative(str(entry["target"]))
            staged = staging_root / relative
            target = sd_root / relative
            backup = rollback_root / "files" / relative if target.exists() else None
            target.parent.mkdir(parents=True, exist_ok=True)
            if backup:
                backup.parent.mkdir(parents=True, exist_ok=True)
                os.replace(target, backup)
            try:
                os.replace(staged, target)
            except Exception:
                if backup and backup.exists():
                    os.replace(backup, target)
                raise
            installed.append((target, backup))

        if groups is None or "arf" in groups:
            for cleanup in manifest.get("cleanup", []):
                legacy = sd_root / ext_relative(str(cleanup["legacy"]))
                canonical = sd_root / ext_relative(str(cleanup["canonical"]))
                if legacy.exists() and canonical.exists() and legacy != canonical:
                    backup = rollback_root / "legacy" / legacy.relative_to(sd_root)
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(legacy, backup)
                    removed_legacy.append((legacy, backup))

        for _, entry in entries:
            target = sd_root / ext_relative(str(entry["target"]))
            if sha256(target) != entry["sha256"]:
                raise PackageError(f"Installed SHA-256 mismatch: {target}")

        state = {
            "schema": 1,
            "release_id": manifest["release_id"],
            "transaction": transaction,
            "groups": selected_groups,
            "files": [
                {"target": entry["target"], "sha256": entry["sha256"]}
                for _, entry in entries
            ],
            "rollback": f"/.tumoflip/rollback/{transaction}",
        }
        state_path = metadata_root / "install-state.json"
        state_tmp = state_path.with_suffix(".tmp")
        state_tmp.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")
        os.replace(state_tmp, state_path)
        package_state_path = metadata_root / PACKAGE_STATE_FILE
        package_state_tmp = package_state_path.with_suffix(".tmp")
        cleanup_candidates = 0
        if groups is None or "arf" in selected_groups:
            cleanup_candidates = len(manifest.get("cleanup", []))
        package_state_tmp.write_text(
            package_state_text(
                manifest,
                transaction,
                selected_groups,
                len(entries),
                cleanup_candidates,
            ),
            encoding="utf-8",
        )
        os.replace(package_state_tmp, package_state_path)
        shutil.rmtree(staging_root)
        return state
    except Exception:
        for legacy, backup in reversed(removed_legacy):
            if backup.exists():
                legacy.parent.mkdir(parents=True, exist_ok=True)
                os.replace(backup, legacy)
        for target, backup in reversed(installed):
            target.unlink(missing_ok=True)
            if backup and backup.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(backup, target)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("resources_root", type=Path)
    parser.add_argument("sd_root", type=Path)
    parser.add_argument("--group", action="append", dest="groups")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        result = apply_packages(
            args.manifest,
            args.resources_root,
            args.sd_root,
            set(args.groups) if args.groups else None,
            args.dry_run,
        )
    except (KeyError, OSError, PackageError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
