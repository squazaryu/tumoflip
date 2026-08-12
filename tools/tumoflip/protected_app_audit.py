#!/usr/bin/env python3
"""Audit protected Tumoflip apps against an all-the-plugins release.

The generated ledger is intentionally byte-specific. TumoCompanion may suppress a
protected-app DIFF only when the selected release tag, both archive SHA-256 values,
the routed target, and the artifact MD5 all match a published audit entry.

Unknown sources, changed author refs, route drift, missing files, and malformed
decisions fail closed. The tool never executes code from the upstream archives.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any, Iterable


SCHEMA = 2
SOURCE_REPOSITORY = "xMasterX/all-the-plugins"
ACCEPTED_DISPOSITIONS = {
    "sourceMatches",
    "auditedDifference",
    "intentionallyReplaced",
}
DECISION_DISPOSITIONS = ACCEPTED_DISPOSITIONS | {"rejected"}
HEX_32 = re.compile(r"^[0-9a-f]{32}$")
HEX_40 = re.compile(r"^[0-9a-f]{40}$")
HEX_64 = re.compile(r"^[0-9a-f]{64}$")


class AuditError(RuntimeError):
    """Raised when an audit input is incomplete or unsafe."""


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise AuditError(f"invalid JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise AuditError(f"JSON root must be an object: {path}")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=False, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def file_hash(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def bytes_hash(data: bytes, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    digest.update(data)
    return digest.hexdigest()


def semantic_audit_payload(audit: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in audit.items() if key != "generatedAt"}


def semantic_audit_sha256(audit: dict[str, Any]) -> str:
    encoded = json.dumps(
        semantic_audit_payload(audit),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")
    return bytes_hash(encoded, "sha256")


def require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise AuditError(f"{label} must be a non-empty string")
    return value


def validate_registry(registry: dict[str, Any]) -> list[dict[str, Any]]:
    if registry.get("schema") != SCHEMA:
        raise AuditError(f"registry must use schema {SCHEMA}")
    if registry.get("sourceRepository") != SOURCE_REPOSITORY:
        raise AuditError(f"registry sourceRepository must be {SOURCE_REPOSITORY}")
    protected_keys = registry.get("protectedKeys")
    if (
        not isinstance(protected_keys, list)
        or not protected_keys
        or not all(
            isinstance(value, str)
            and value == value.lower()
            and re.fullmatch(r"[a-z0-9_]+", value)
            for value in protected_keys
        )
        or protected_keys != sorted(set(protected_keys))
    ):
        raise AuditError("registry protectedKeys must be sorted unique lowercase stems")
    protected_key_set = set(protected_keys)
    raw_families = registry.get("protectedDataFamilies")
    if not isinstance(raw_families, list):
        raise AuditError("registry protectedDataFamilies must be an array")
    family_prefixes: set[str] = set()
    for family in raw_families:
        if not isinstance(family, dict):
            raise AuditError("protected data family must be an object")
        prefix = require_string(family.get("prefix"), "protected data family prefix")
        owner = require_string(family.get("owner"), "protected data family owner")
        if not prefix.startswith("/ext/apps_data/") or not prefix.endswith("/"):
            raise AuditError(f"invalid protected data family prefix: {prefix}")
        if prefix in family_prefixes:
            raise AuditError(f"duplicate protected data family prefix: {prefix}")
        if owner not in protected_key_set:
            raise AuditError(f"protected data family owner is not protected: {owner}")
        family_prefixes.add(prefix)
    raw_apps = registry.get("apps")
    if not isinstance(raw_apps, list) or not raw_apps:
        raise AuditError("registry apps must be a non-empty array")

    apps: list[dict[str, Any]] = []
    app_ids: set[str] = set()
    aliases: set[str] = set()
    archive_paths: set[str] = set()
    target_paths: set[str] = set()
    for index, raw_app in enumerate(raw_apps):
        if not isinstance(raw_app, dict):
            raise AuditError(f"registry app {index} must be an object")
        app_id = require_string(raw_app.get("id"), f"apps[{index}].id")
        if app_id in app_ids:
            raise AuditError(f"duplicate registry app id: {app_id}")
        app_ids.add(app_id)
        raw_aliases = raw_app.get("aliases", [])
        if not isinstance(raw_aliases, list) or not all(
            isinstance(alias, str) and alias for alias in raw_aliases
        ):
            raise AuditError(f"aliases must be strings for {app_id}")
        for alias in (app_id, *raw_aliases):
            if alias in aliases:
                raise AuditError(f"duplicate protected alias: {alias}")
            if alias not in protected_key_set:
                raise AuditError(f"registry app alias is not in protectedKeys: {alias}")
            aliases.add(alias)
        require_string(raw_app.get("packSourcePath"), f"{app_id}.packSourcePath")
        require_string(raw_app.get("localSourcePath"), f"{app_id}.localSourcePath")
        author = raw_app.get("author")
        if not isinstance(author, dict):
            raise AuditError(f"author must be an object for {app_id}")
        require_string(author.get("repository"), f"{app_id}.author.repository")
        require_string(author.get("ref"), f"{app_id}.author.ref")
        reviewed = require_string(
            author.get("lastReviewedCommit"), f"{app_id}.author.lastReviewedCommit"
        )
        if not HEX_40.fullmatch(reviewed):
            raise AuditError(f"invalid reviewed author commit for {app_id}: {reviewed}")
        disposition = raw_app.get("defaultDisposition")
        if disposition not in ACCEPTED_DISPOSITIONS:
            raise AuditError(f"invalid default disposition for {app_id}: {disposition}")
        artifacts = raw_app.get("artifacts")
        if not isinstance(artifacts, list) or not artifacts:
            raise AuditError(f"artifacts must be a non-empty array for {app_id}")
        for artifact in artifacts:
            _validate_artifact_spec(app_id, artifact, archive_paths, target_paths)
        family = raw_app.get("artifactFamily")
        if family is not None:
            if not isinstance(family, dict):
                raise AuditError(f"artifactFamily must be an object for {app_id}")
            if family.get("pack") not in {"base", "extra"}:
                raise AuditError(f"invalid family pack for {app_id}")
            for key in ("archivePrefix", "remotePrefix", "targetPrefix", "extension"):
                require_string(family.get(key), f"{app_id}.artifactFamily.{key}")
            expected = family.get("expectedCount")
            if not isinstance(expected, int) or isinstance(expected, bool) or expected < 1:
                raise AuditError(f"invalid family expectedCount for {app_id}")
        apps.append(raw_app)
    return apps


def _validate_artifact_spec(
    app_id: str,
    artifact: Any,
    archive_paths: set[str],
    target_paths: set[str],
) -> None:
    if not isinstance(artifact, dict):
        raise AuditError(f"artifact must be an object for {app_id}")
    if artifact.get("pack") not in {"base", "extra"}:
        raise AuditError(f"invalid artifact pack for {app_id}")
    archive_path = require_string(
        artifact.get("archivePath"), f"{app_id}.artifact.archivePath"
    )
    remote_path = require_string(
        artifact.get("remotePath"), f"{app_id}.artifact.remotePath"
    )
    target_path = require_string(
        artifact.get("targetPath"), f"{app_id}.artifact.targetPath"
    )
    if archive_path.startswith("/") or ".." in Path(archive_path).parts:
        raise AuditError(f"unsafe archive path for {app_id}: {archive_path}")
    if not remote_path.startswith("/ext/") or not target_path.startswith("/ext/"):
        raise AuditError(f"artifact paths must stay below /ext for {app_id}")
    if archive_path in archive_paths:
        raise AuditError(f"duplicate protected archive path: {archive_path}")
    if target_path in target_paths and not target_path.startswith(
        "/ext/apps_data/totp/plugins/"
    ):
        raise AuditError(f"duplicate protected target path: {target_path}")
    archive_paths.add(archive_path)
    target_paths.add(target_path)


def load_archive(path: Path, expected_sha256: str) -> dict[str, bytes]:
    if not HEX_64.fullmatch(expected_sha256):
        raise AuditError(f"invalid expected SHA-256 for {path.name}")
    actual = file_hash(path, "sha256")
    if actual != expected_sha256:
        raise AuditError(f"archive SHA-256 differs for {path.name}: {actual} != {expected_sha256}")
    entries: dict[str, bytes] = {}
    try:
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if info.is_dir():
                    continue
                name = info.filename
                if name.startswith("/") or ".." in Path(name).parts or "\\" in name:
                    raise AuditError(f"unsafe ZIP member: {name}")
                if name in entries:
                    raise AuditError(f"duplicate ZIP member: {name}")
                entries[name] = archive.read(info)
    except (OSError, zipfile.BadZipFile) as error:
        raise AuditError(f"invalid ZIP archive {path}: {error}") from error
    return entries


def git_path_changed(repo: Path, before: str, after: str, path: str) -> bool:
    for commit in (before, after):
        result = subprocess.run(
            ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
            cwd=repo,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if result.returncode:
            raise AuditError(f"required source commit is unavailable: {commit}")
    result = subprocess.run(
        ["git", "diff", "--quiet", before, after, "--", path],
        cwd=repo,
        check=False,
    )
    if result.returncode not in (0, 1):
        raise AuditError(f"git diff failed for protected source path: {path}")
    return result.returncode == 1


def fetch_author_head(repository: str, ref: str) -> str:
    try:
        result = subprocess.run(
            ["git", "ls-remote", repository, ref],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AuditError(f"author source fetch failed for {repository} {ref}: {error}") from error
    if result.returncode:
        raise AuditError(f"author source fetch failed for {repository} {ref}")
    rows = [row.split() for row in result.stdout.splitlines() if row.strip()]
    if len(rows) != 1 or len(rows[0]) < 2 or not HEX_40.fullmatch(rows[0][0]):
        raise AuditError(f"author ref is missing or ambiguous: {repository} {ref}")
    return rows[0][0]


def load_author_heads(path: Path | None) -> dict[str, str]:
    if path is None:
        return {}
    document = read_json(path)
    heads = document.get("heads")
    if not isinstance(heads, dict):
        raise AuditError("author heads fixture must contain a heads object")
    for app_id, commit in heads.items():
        if not isinstance(app_id, str) or not isinstance(commit, str) or not HEX_40.fullmatch(commit):
            raise AuditError("author heads fixture contains an invalid entry")
    return dict(heads)


def load_decisions(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    document = read_json(path)
    if document.get("schema") != SCHEMA:
        raise AuditError(f"decisions must use schema {SCHEMA}")
    raw = document.get("decisions")
    if not isinstance(raw, list):
        raise AuditError("decisions must be an array")
    decisions: dict[str, dict[str, Any]] = {}
    for item in raw:
        if not isinstance(item, dict):
            raise AuditError("decision must be an object")
        app_id = require_string(item.get("appId"), "decision.appId")
        if app_id in decisions:
            raise AuditError(f"duplicate decision for {app_id}")
        if item.get("disposition") not in DECISION_DISPOSITIONS:
            raise AuditError(f"invalid decision disposition for {app_id}")
        author_commit = require_string(
            item.get("throughAuthorCommit"), f"{app_id}.throughAuthorCommit"
        )
        source_commit = require_string(item.get("sourceCommit"), f"{app_id}.sourceCommit")
        if not HEX_40.fullmatch(author_commit) or not HEX_40.fullmatch(source_commit):
            raise AuditError(f"invalid decision commit for {app_id}")
        require_string(item.get("changelog"), f"{app_id}.changelog")
        disposition = item["disposition"]
        if disposition == "auditedDifference":
            implementation = require_string(
                item.get("implementationCommit"), f"{app_id}.implementationCommit"
            )
            if not HEX_40.fullmatch(implementation):
                raise AuditError(f"invalid implementation commit for {app_id}")
        if disposition in {"auditedDifference", "sourceMatches", "rejected"}:
            package = item.get("fwPackages")
            if not isinstance(package, dict):
                raise AuditError(f"target-bearing decision requires fwPackages for {app_id}")
            if package.get("channel") not in {"stable", "dev"}:
                raise AuditError(f"invalid FW Packages channel for {app_id}")
            revision = package.get("revision")
            if not isinstance(revision, int) or isinstance(revision, bool) or revision < 1:
                raise AuditError(f"invalid FW Packages revision for {app_id}")
            release_tag = require_string(
                package.get("releaseTag"), f"{app_id}.fwPackages.releaseTag"
            )
            expected_tag = f"fw-packages-{package['channel']}-{revision:03d}"
            if release_tag != expected_tag:
                raise AuditError(
                    f"FW Packages revision/tag mismatch for {app_id}: "
                    f"{release_tag} != {expected_tag}"
                )
        if disposition in {"auditedDifference", "sourceMatches"}:
            if item.get("hardwareAccepted") is not True:
                raise AuditError(
                    f"changed protected app requires hardwareAccepted=true for {app_id}"
                )
        decisions[app_id] = item
    return decisions


def git_commit_is_ancestor(repo: Path, commit: str, descendant: str) -> bool:
    for value in (commit, descendant):
        if subprocess.run(
            ["git", "cat-file", "-e", f"{value}^{{commit}}"],
            cwd=repo,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode:
            return False
    return subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, descendant],
        cwd=repo,
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def load_target_manifests(paths: list[Path]) -> list[dict[str, Any]]:
    if not paths:
        raise AuditError("at least one exact target manifest is required")
    result: list[dict[str, Any]] = []
    identities: set[tuple[str, str, str]] = set()
    for path in paths:
        document = read_json(path)
        if document.get("schema") != 2:
            raise AuditError(f"target manifest must use schema 2: {path}")
        release = document.get("package_release")
        if not isinstance(release, dict):
            raise AuditError(f"target manifest has no package_release: {path}")
        channel = release.get("catalog_channel")
        release_tag = release.get("catalog_release_tag")
        if channel not in {"stable", "dev"}:
            raise AuditError(f"target manifest channel is invalid: {path}")
        release_tag = require_string(release_tag, f"{path}.catalog_release_tag")
        revision = release.get("catalog_revision")
        if not isinstance(revision, int) or isinstance(revision, bool) or revision < 1:
            raise AuditError(f"target manifest revision is invalid: {path}")
        expected_tag = f"fw-packages-{channel}-{revision:03d}"
        if release_tag != expected_tag:
            raise AuditError(
                f"target manifest revision/tag mismatch: {release_tag} != {expected_tag}"
            )
        source_commit = require_string(
            release.get("source_commit"), f"{path}.source_commit"
        )
        if not HEX_40.fullmatch(source_commit):
            raise AuditError(f"target manifest source commit is invalid: {path}")
        if release.get("source_dirty") is not False:
            raise AuditError(f"target manifest source must be clean: {path}")
        target_release_tag = require_string(
            release.get("target_release_tag"), f"{path}.target_release_tag"
        )
        target_release_id = require_string(
            release.get("target_release_id"), f"{path}.target_release_id"
        )
        if not HEX_64.fullmatch(target_release_id):
            raise AuditError(f"target manifest release id is invalid: {path}")
        manifest_sha256 = file_hash(path, "sha256")
        targets: dict[str, dict[str, Any]] = {}
        packages = document.get("packages")
        if not isinstance(packages, dict):
            raise AuditError(f"target manifest packages are invalid: {path}")
        for entries in packages.values():
            if not isinstance(entries, list):
                raise AuditError(f"target manifest package group is invalid: {path}")
            for entry in entries:
                if not isinstance(entry, dict):
                    raise AuditError(f"target manifest entry is invalid: {path}")
                target = entry.get("target")
                md5 = entry.get("md5")
                sha256 = entry.get("sha256")
                size = entry.get("bytes")
                if not isinstance(target, str) or not target.startswith("/ext/"):
                    raise AuditError(f"target manifest path is invalid: {path}")
                if not isinstance(md5, str) or not HEX_32.fullmatch(md5):
                    raise AuditError(f"target manifest MD5 is invalid for {target}: {path}")
                if not isinstance(sha256, str) or not HEX_64.fullmatch(sha256):
                    raise AuditError(
                        f"target manifest SHA-256 is invalid for {target}: {path}"
                    )
                if not isinstance(size, int) or isinstance(size, bool) or size < 0:
                    raise AuditError(f"target manifest size is invalid for {target}: {path}")
                if target in targets:
                    raise AuditError(f"duplicate target manifest path: {target}")
                targets[target] = {"md5": md5, "sha256": sha256, "bytes": size}
        identity = (channel, release_tag, manifest_sha256)
        if identity in identities:
            raise AuditError(f"duplicate target manifest provenance: {path}")
        identities.add(identity)
        result.append(
            {
                "channel": channel,
                "releaseTag": release_tag,
                "revision": revision,
                "sourceCommit": source_commit,
                "targetReleaseTag": target_release_tag,
                "targetReleaseID": target_release_id,
                "manifestSHA256": manifest_sha256,
                "targets": targets,
            }
        )
    return result


def load_target_archives(
    paths: list[Path], manifests: list[dict[str, Any]]
) -> dict[tuple[str, str], dict[str, Any]]:
    if not paths:
        return {}
    manifest_keys = {(item["channel"], item["releaseTag"]) for item in manifests}
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for path in paths:
        name_without_suffix = path.name.removesuffix(".zip")
        stem_parts = name_without_suffix.split("=", 1)
        if len(stem_parts) != 2 or ":" not in stem_parts[0]:
            raise AuditError(
                "target archive filename must be channel:releaseTag=label.zip: "
                f"{path.name}"
            )
        channel, release_tag = stem_parts[0].split(":", 1)
        key = (channel, release_tag)
        if key not in manifest_keys or key in result:
            raise AuditError(f"target archive identity is unknown or duplicated: {path.name}")
        try:
            with zipfile.ZipFile(path) as archive:
                targets: dict[str, dict[str, Any]] = {}
                for info in archive.infolist():
                    if info.is_dir():
                        continue
                    name = info.filename
                    if name.startswith("/") or ".." in Path(name).parts or "\\" in name:
                        raise AuditError(f"unsafe target archive member: {name}")
                    target = "/ext/" + name
                    if target in targets:
                        raise AuditError(f"duplicate target archive member: {name}")
                    data = archive.read(info)
                    targets[target] = {
                        "md5": bytes_hash(data, "md5"),
                        "sha256": bytes_hash(data, "sha256"),
                        "bytes": len(data),
                    }
        except (OSError, zipfile.BadZipFile) as error:
            raise AuditError(f"invalid target archive {path}: {error}") from error
        result[key] = {
            "sha256": file_hash(path, "sha256"),
            "targets": targets,
        }
    return result


def verify_target_archives(
    manifests: list[dict[str, Any]],
    archives: dict[tuple[str, str], dict[str, Any]],
) -> None:
    for manifest in manifests:
        key = (manifest["channel"], manifest["releaseTag"])
        archive = archives.get(key)
        if archive is None:
            raise AuditError(
                "exact FW Packages ZIP is missing for "
                f"{manifest['channel']}:{manifest['releaseTag']}"
            )
        manifest_targets = manifest["targets"]
        archive_targets = archive["targets"]
        if set(manifest_targets) != set(archive_targets):
            missing = sorted(set(manifest_targets) - set(archive_targets))
            unexpected = sorted(set(archive_targets) - set(manifest_targets))
            raise AuditError(
                "FW Packages manifest/ZIP paths differ for "
                f"{manifest['releaseTag']}: missing={missing[:3]} "
                f"unexpected={unexpected[:3]}"
            )
        for target, expected in manifest_targets.items():
            if archive_targets[target] != expected:
                raise AuditError(
                    f"FW Packages manifest/ZIP bytes differ for {target}: "
                    f"{manifest['releaseTag']}"
                )


def materialize_artifacts(
    app: dict[str, Any], archives: dict[str, dict[str, bytes]]
) -> list[dict[str, str]]:
    specs = [dict(spec) for spec in app["artifacts"]]
    family = app.get("artifactFamily")
    if family is not None:
        members = sorted(
            path
            for path in archives[family["pack"]]
            if path.startswith(family["archivePrefix"])
            and path.endswith(family["extension"])
        )
        if len(members) != family["expectedCount"]:
            raise AuditError(
                f"{app['id']} artifact family differs: "
                f"{len(members)} != {family['expectedCount']}"
            )
        for archive_path in members:
            filename = archive_path.removeprefix(family["archivePrefix"])
            if "/" in filename or not filename:
                raise AuditError(f"nested or empty family member for {app['id']}: {archive_path}")
            specs.append(
                {
                    "pack": family["pack"],
                    "archivePath": archive_path,
                    "remotePath": family["remotePrefix"] + filename,
                    "targetPath": family["targetPrefix"] + filename,
                }
            )

    result: list[dict[str, str]] = []
    for spec in specs:
        data = archives[spec["pack"]].get(spec["archivePath"])
        if data is None:
            raise AuditError(
                f"protected artifact is missing: {spec['pack']}:{spec['archivePath']}"
            )
        result.append(
            {
                "pack": spec["pack"],
                "remotePath": spec["remotePath"],
                "targetPath": spec["targetPath"],
                "sourceMD5": hashlib.md5(data).hexdigest(),
            }
        )
    return sorted(result, key=lambda item: (item["remotePath"], item["targetPath"]))


def validate_protected_intersections(
    registry: dict[str, Any],
    apps: list[dict[str, Any]],
    artifacts_by_app: dict[str, list[dict[str, str]]],
    archives: dict[str, dict[str, bytes]],
) -> None:
    known: set[tuple[str, str]] = set()
    protected_keys = set(registry["protectedKeys"])
    data_families = registry["protectedDataFamilies"]
    for app in apps:
        for artifact in artifacts_by_app[app["id"]]:
            known.add((artifact["pack"], artifact["remotePath"]))

    for pack, members in archives.items():
        prefix = f"{pack}_pack_build/artifacts-{pack}/"
        for archive_path in members:
            if not archive_path.endswith((".fap", ".fal")):
                continue
            if archive_path.startswith(prefix):
                remote_path = "/ext/apps/" + archive_path.removeprefix(prefix)
            elif archive_path.startswith(f"{pack}_pack_build/apps_data/"):
                remote_path = "/ext/apps_data/" + archive_path.removeprefix(
                    f"{pack}_pack_build/apps_data/"
                )
            else:
                continue
            keys = {Path(remote_path).stem.lower()}
            family = next(
                (
                    item
                    for item in data_families
                    if remote_path.startswith(item["prefix"])
                ),
                None,
            )
            if family is not None:
                keys.add(family["owner"])
            elif remote_path.startswith("/ext/apps_data/"):
                suffix = remote_path.removeprefix("/ext/apps_data/")
                keys.add(suffix.split("/", 1)[0].lower())
            if keys & protected_keys and (pack, remote_path) not in known:
                raise AuditError(f"unregistered protected artifact: {pack}:{remote_path}")


def audit_release(args: argparse.Namespace) -> tuple[dict[str, Any], str]:
    registry = read_json(args.registry)
    apps = validate_registry(registry)
    if not HEX_40.fullmatch(args.source_commit) or not HEX_40.fullmatch(
        args.previous_source_commit
    ):
        raise AuditError("source commits must be full 40-character Git hashes")
    archives = {
        "base": load_archive(args.base_archive, args.base_sha256),
        "extra": load_archive(args.extra_archive, args.extra_sha256),
    }
    artifacts_by_app = {
        app["id"]: materialize_artifacts(app, archives) for app in apps
    }
    validate_protected_intersections(registry, apps, artifacts_by_app, archives)

    decisions = load_decisions(args.decisions)
    fixture_heads = load_author_heads(args.author_heads)
    target_manifests = load_target_manifests(args.target_manifest)
    target_archives = load_target_archives(args.target_archive, target_manifests)
    verify_target_archives(target_manifests, target_archives)
    decision_manifests: dict[str, dict[str, Any]] = {}
    for app_id, decision in decisions.items():
        if app_id not in {app["id"] for app in apps}:
            raise AuditError(f"decision references an unknown protected app: {app_id}")
        package = decision.get("fwPackages")
        decision_manifest = next(
            (
                manifest
                for manifest in target_manifests
                if isinstance(package, dict)
                and manifest["channel"] == package["channel"]
                and manifest["releaseTag"] == package["releaseTag"]
                and manifest["revision"] == package["revision"]
            ),
            None,
        )
        if isinstance(package, dict) and decision_manifest is None:
            raise AuditError(f"FW Packages decision has no exact target manifest for {app_id}")
        if decision_manifest is not None:
            decision_manifests[app_id] = decision_manifest
        if decision.get("disposition") == "auditedDifference":
            if decision_manifest is None or not git_commit_is_ancestor(
                args.implementation_repo,
                decision["implementationCommit"],
                decision_manifest["sourceCommit"],
            ):
                raise AuditError(
                    "implementation commit is not reachable from the exact FW Packages "
                    f"source commit for {app_id}"
                )
    accepted_entries: list[dict[str, Any]] = []
    app_results: list[dict[str, Any]] = []
    unresolved: list[str] = []

    for app in apps:
        app_id = app["id"]
        author = app["author"]
        try:
            source_changed = git_path_changed(
                args.repo,
                args.previous_source_commit,
                args.source_commit,
                app["packSourcePath"],
            )
        except AuditError as error:
            source_changed = True
            source_error = str(error)
        else:
            source_error = None

        if author["ref"] == "release-source":
            author_head = args.source_commit
            author_changed = source_changed
        else:
            try:
                author_head = fixture_heads.get(app_id) or fetch_author_head(
                    author["repository"], author["ref"]
                )
                author_changed = author_head != author["lastReviewedCommit"]
                author_error = None
            except AuditError as error:
                author_head = None
                author_changed = True
                author_error = str(error)
        if author["ref"] == "release-source":
            author_error = None

        decision = decisions.get(app_id)
        decision_matches = bool(
            decision
            and decision["sourceCommit"] == args.source_commit
            and author_head is not None
            and decision["throughAuthorCommit"] == author_head
        )
        source_reviewed = not (source_changed or author_changed) or decision_matches
        decision_disposition = decision["disposition"] if decision_matches else None
        disposition = (
            "auditedDifference"
            if decision_disposition == "rejected"
            else decision_disposition or app["defaultDisposition"]
        )
        note = decision.get("changelog") if decision_matches else app.get("note", "")
        artifact_results: list[dict[str, Any]] = []
        app_unresolved: list[str] = []
        for artifact in artifacts_by_app[app_id]:
            artifact_result: dict[str, Any] = dict(artifact)
            if not source_reviewed:
                reasons = []
                if source_changed:
                    reasons.append("pack source changed")
                if author_changed:
                    reasons.append("author source changed or unavailable")
                if decision and not decision_matches:
                    reasons.append("decision does not match exact source refs")
                reason = ", ".join(reasons) or "review decision is missing"
                artifact_result["ledgerStatus"] = "needsReview"
                app_unresolved.append(f"{app_id}:{artifact['remotePath']}: {reason}")
                artifact_results.append(artifact_result)
                continue

            entry: dict[str, Any] = {
                "remotePath": artifact["remotePath"],
                "targetPath": artifact["targetPath"],
                "sourceMD5": artifact["sourceMD5"],
                "disposition": disposition,
                "note": note,
            }
            if disposition == "intentionallyReplaced":
                present_in = [
                    f"{manifest['channel']}:{manifest['releaseTag']}"
                    for manifest in target_manifests
                    if artifact["targetPath"] in manifest["targets"]
                ]
                if present_in:
                    artifact_result["ledgerStatus"] = "needsReview"
                    app_unresolved.append(
                        f"{app_id}:{artifact['remotePath']}: intentionally replaced target "
                        f"is still shipped by exact FW Packages ({', '.join(present_in)})"
                    )
                    artifact_results.append(artifact_result)
                    continue
                entry["targetMD5s"] = []
                entry["targetProvenance"] = []
                artifact_result["ledgerStatus"] = "accepted"
                accepted_entries.append(entry)
                artifact_results.append(artifact_result)
                continue

            evidence_manifests = target_manifests
            if decision_matches and decision_disposition in {
                "auditedDifference",
                "sourceMatches",
                "rejected",
            }:
                evidence_manifests = [decision_manifests[app_id]]

            provenance: list[dict[str, Any]] = []
            for manifest in evidence_manifests:
                target = manifest["targets"].get(artifact["targetPath"])
                if target is None:
                    continue
                archive = target_archives[(manifest["channel"], manifest["releaseTag"])]
                provenance.append(
                    {
                        "targetMD5": target["md5"],
                        "channel": manifest["channel"],
                        "releaseTag": manifest["releaseTag"],
                        "manifestSHA256": manifest["manifestSHA256"],
                        "containerKind": "fwPackagesZip",
                        "containerSHA256": archive["sha256"],
                        "targetReleaseTag": manifest["targetReleaseTag"],
                        "targetSourceCommit": manifest["sourceCommit"],
                    }
                )
            if not provenance:
                artifact_result["ledgerStatus"] = "needsReview"
                app_unresolved.append(
                    f"{app_id}:{artifact['remotePath']}: target absent from exact "
                    "FW Packages manifest/ZIP"
                )
                artifact_results.append(artifact_result)
                continue

            target_md5s = sorted({item["targetMD5"] for item in provenance})
            operational_disposition = (
                "sourceMatches"
                if set(target_md5s) == {artifact["sourceMD5"]}
                else "auditedDifference"
            )
            if disposition == "sourceMatches" and operational_disposition != "sourceMatches":
                raise AuditError(
                    f"sourceMatches decision has different target bytes for {app_id}: "
                    f"{artifact['targetPath']}"
                )
            entry["disposition"] = operational_disposition
            entry["targetMD5s"] = target_md5s
            entry["targetProvenance"] = sorted(
                provenance,
                key=lambda item: (
                    item["channel"],
                    item["releaseTag"],
                    item["targetMD5"],
                ),
            )
            artifact_result["ledgerStatus"] = "accepted"
            accepted_entries.append(entry)
            artifact_results.append(artifact_result)

        unresolved.extend(app_unresolved)
        result: dict[str, Any] = {
            "appId": app_id,
            "aliases": app.get("aliases", []),
            "packSourcePath": app["packSourcePath"],
            "localSourcePath": app["localSourcePath"],
            "packSourceChanged": source_changed,
            "author": {
                "repository": author["repository"],
                "ref": author["ref"],
                "previousCommit": author["lastReviewedCommit"],
                "currentCommit": author_head,
                "changed": author_changed,
                "license": author["license"],
            },
            "status": "needsReview" if app_unresolved else "verified",
            "disposition": "needsReview" if app_unresolved else disposition,
            "note": note,
            "artifacts": artifact_results,
        }
        if decision_disposition == "rejected":
            result["decisionDisposition"] = "rejected"
        if source_error:
            result["sourceError"] = source_error
        if author_error:
            result["author"]["error"] = author_error
        if decision_matches:
            result["decision"] = decision
        app_results.append(result)

    now = args.generated_at or dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    audit: dict[str, Any] = {
        "sequence": args.sequence,
        "sourceTag": args.source_tag,
        "sourceCommit": args.source_commit,
        "sourceURL": args.release_url,
        "publishedAt": args.published_at,
        "api": args.api,
        "auditIssue": args.issue_url,
        "issue": {
            "number": args.issue_number,
            "url": args.issue_url,
            "state": "open" if unresolved else "closed",
        },
        "overallStatus": "pending" if unresolved else "verified",
        "archives": [
            {
                "pack": "base",
                "fileName": args.base_archive.name,
                "sha256": args.base_sha256,
            },
            {
                "pack": "extra",
                "fileName": args.extra_archive.name,
                "sha256": args.extra_sha256,
            },
        ],
        "entries": sorted(
            accepted_entries, key=lambda item: (item["remotePath"], item["targetPath"])
        ),
        "apps": app_results,
        "unresolved": unresolved,
        "generatedAt": now,
    }
    summary = render_issue_body(audit)
    return audit, summary


def render_issue_body(audit: dict[str, Any]) -> str:
    archive_lines = "\n".join(
        f"- `{item['fileName']}`: `{item['sha256']}`" for item in audit["archives"]
    )
    app_lines = []
    for app in audit["apps"]:
        marker = "x" if app["status"] == "verified" else " "
        detail = app["disposition"]
        if app["packSourceChanged"]:
            detail += "; pack source changed"
        if app["author"]["changed"]:
            detail += "; author source changed"
        app_lines.append(
            f"- [{marker}] `{app['appId']}` — {detail}; "
            f"{len(app['artifacts'])} artifact(s)"
        )
    unresolved = "\n".join(f"- {value}" for value in audit["unresolved"]) or "- none"
    return f"""<!-- tumoflip-protected-app-audit -->
## Community Pack audit

- Release: [{audit['sourceTag']}]({audit['sourceURL']})
- Commit: `{audit['sourceCommit']}`
- API: `{audit['api']}`
- Status: **{audit['overallStatus']}**

### Archive identity

{archive_lines}

### Protected intersections

{chr(10).join(app_lines)}

### Unresolved

{unresolved}

`DIFF` is suppressed only for exact archive bytes, source MD5 and routed target
entries published in the protected-app audit ledger. A missing or changed entry
remains fail-closed. When a port is required, record its source commit, changelog,
implementation commit and FW Packages revision before closing this issue.
"""


def validate_audit(audit: dict[str, Any]) -> None:
    sequence = audit.get("sequence")
    if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 1:
        raise AuditError("audit sequence is invalid")
    require_string(audit.get("sourceTag"), "audit.sourceTag")
    source_commit = require_string(audit.get("sourceCommit"), "audit.sourceCommit")
    if not HEX_40.fullmatch(source_commit):
        raise AuditError("audit sourceCommit is invalid")
    if audit.get("overallStatus") not in {"pending", "verified"}:
        raise AuditError("audit overallStatus is invalid")
    audit_issue = require_string(audit.get("auditIssue"), "audit.auditIssue")
    if not re.fullmatch(r"https://github\.com/[^/]+/[^/]+/issues/[1-9][0-9]*", audit_issue):
        raise AuditError("audit auditIssue must be an exact GitHub issue URL")
    archives = audit.get("archives")
    if not isinstance(archives, list) or len(archives) != 2:
        raise AuditError("audit must contain base and extra archives")
    packs = set()
    for archive in archives:
        if not isinstance(archive, dict) or archive.get("pack") not in {"base", "extra"}:
            raise AuditError("audit archive is invalid")
        packs.add(archive["pack"])
        require_string(archive.get("fileName"), "audit archive filename")
        digest = require_string(archive.get("sha256"), "audit archive sha256")
        if not HEX_64.fullmatch(digest):
            raise AuditError("audit archive SHA-256 is invalid")
    if packs != {"base", "extra"}:
        raise AuditError("audit archive packs must be unique")
    entries = audit.get("entries")
    if not isinstance(entries, list):
        raise AuditError("audit entries must be an array")
    seen: set[tuple[str, str, str]] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise AuditError("audit entry must be an object")
        remote = require_string(entry.get("remotePath"), "entry.remotePath")
        target = require_string(entry.get("targetPath"), "entry.targetPath")
        digest = require_string(entry.get("sourceMD5"), "entry.sourceMD5")
        if not remote.startswith("/ext/") or not target.startswith("/ext/"):
            raise AuditError("audit entry paths must stay below /ext")
        if not HEX_32.fullmatch(digest):
            raise AuditError("audit entry MD5 is invalid")
        disposition = entry.get("disposition")
        if disposition not in ACCEPTED_DISPOSITIONS:
            raise AuditError("unresolved disposition cannot be published as an entry")
        if disposition in {"auditedDifference", "sourceMatches"}:
            target_md5s = entry.get("targetMD5s")
            provenance = entry.get("targetProvenance")
            if (
                not isinstance(target_md5s, list)
                or not target_md5s
                or not all(isinstance(value, str) and HEX_32.fullmatch(value) for value in target_md5s)
                or target_md5s != sorted(set(target_md5s))
            ):
                raise AuditError(
                    "target-bearing entry targetMD5s must be unique lowercase MD5s"
                )
            if not isinstance(provenance, list) or not provenance:
                raise AuditError("target-bearing entry targetProvenance is required")
            provenance_hashes = []
            provenance_identities: set[tuple[str, str, str, str]] = set()
            for item in provenance:
                if not isinstance(item, dict):
                    raise AuditError("target provenance must be an object")
                target_md5 = item.get("targetMD5")
                channel = item.get("channel")
                release_tag = item.get("releaseTag")
                manifest_sha = item.get("manifestSHA256")
                container_kind = item.get("containerKind")
                container_sha = item.get("containerSHA256")
                target_release_tag = item.get("targetReleaseTag")
                target_source_commit = item.get("targetSourceCommit")
                if not isinstance(target_md5, str) or not HEX_32.fullmatch(target_md5):
                    raise AuditError("target provenance MD5 is invalid")
                if channel not in {"stable", "dev"}:
                    raise AuditError("target provenance channel is invalid")
                require_string(release_tag, "target provenance releaseTag")
                if not isinstance(manifest_sha, str) or not HEX_64.fullmatch(manifest_sha):
                    raise AuditError("target provenance manifestSHA256 is invalid")
                if container_kind != "fwPackagesZip":
                    raise AuditError("target provenance containerKind is invalid")
                if not isinstance(container_sha, str) or not HEX_64.fullmatch(container_sha):
                    raise AuditError("target provenance containerSHA256 is invalid")
                require_string(target_release_tag, "target provenance targetReleaseTag")
                if not isinstance(target_source_commit, str) or not HEX_40.fullmatch(
                    target_source_commit
                ):
                    raise AuditError("target provenance targetSourceCommit is invalid")
                identity = (target_md5, channel, release_tag, manifest_sha)
                if identity in provenance_identities:
                    raise AuditError("duplicate target provenance")
                provenance_identities.add(identity)
                provenance_hashes.append(target_md5)
            if set(provenance_hashes) != set(target_md5s):
                raise AuditError("targetMD5s and targetProvenance hashes differ")
            if disposition == "sourceMatches" and set(target_md5s) != {digest}:
                raise AuditError("sourceMatches target MD5 must equal sourceMD5")
        elif entry.get("targetMD5s") != [] or entry.get("targetProvenance") != []:
            raise AuditError("intentionallyReplaced entry must have empty target evidence")
        key = (remote, target, digest)
        if key in seen:
            raise AuditError("duplicate audit entry")
        seen.add(key)
    unresolved = audit.get("unresolved")
    if not isinstance(unresolved, list) or not all(isinstance(item, str) for item in unresolved):
        raise AuditError("audit unresolved must be a string array")
    if audit["overallStatus"] == "verified" and unresolved:
        raise AuditError("verified audit cannot have unresolved apps")
    if audit["overallStatus"] == "pending" and not unresolved:
        raise AuditError("pending audit must identify unresolved artifacts")

    apps = audit.get("apps")
    if not isinstance(apps, list) or not apps:
        raise AuditError("audit apps must be a non-empty array")
    accepted_keys = {
        (entry["remotePath"], entry["targetPath"], entry["sourceMD5"])
        for entry in entries
    }
    artifact_keys: set[tuple[str, str, str]] = set()
    expected_accepted: set[tuple[str, str, str]] = set()
    expected_unresolved = 0
    app_ids: set[str] = set()
    for app in apps:
        if not isinstance(app, dict):
            raise AuditError("audit app must be an object")
        app_id = require_string(app.get("appId"), "audit app id")
        if app_id in app_ids:
            raise AuditError(f"duplicate audit app: {app_id}")
        app_ids.add(app_id)
        artifacts = app.get("artifacts")
        if not isinstance(artifacts, list) or not artifacts:
            raise AuditError(f"audit app artifacts are invalid: {app_id}")
        app_has_unresolved = False
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                raise AuditError(f"audit app artifact is invalid: {app_id}")
            key = (
                require_string(artifact.get("remotePath"), "audit artifact remotePath"),
                require_string(artifact.get("targetPath"), "audit artifact targetPath"),
                require_string(artifact.get("sourceMD5"), "audit artifact sourceMD5"),
            )
            if key in artifact_keys:
                raise AuditError("duplicate audit app artifact")
            artifact_keys.add(key)
            status = artifact.get("ledgerStatus")
            if status == "accepted":
                expected_accepted.add(key)
            elif status == "needsReview":
                expected_unresolved += 1
                app_has_unresolved = True
            else:
                raise AuditError(f"invalid artifact ledgerStatus for {app_id}")
        expected_status = "needsReview" if app_has_unresolved else "verified"
        if app.get("status") != expected_status:
            raise AuditError(f"audit app status disagrees with artifacts: {app_id}")
    if accepted_keys != expected_accepted:
        raise AuditError("audit entries do not exactly cover accepted app artifacts")
    if len(unresolved) != expected_unresolved:
        raise AuditError("audit unresolved list does not cover every unresolved artifact")


def validate_ledger(ledger: dict[str, Any]) -> None:
    if ledger.get("schema") != SCHEMA:
        raise AuditError(f"ledger must use schema {SCHEMA}")
    if ledger.get("sourceRepository") != SOURCE_REPOSITORY:
        raise AuditError(f"ledger sourceRepository must be {SOURCE_REPOSITORY}")
    require_string(ledger.get("generatedAt"), "ledger.generatedAt")
    audits = ledger.get("audits")
    if not isinstance(audits, list):
        raise AuditError("ledger audits must be an array")
    identities: set[tuple[str, str, str]] = set()
    for audit in audits:
        if not isinstance(audit, dict):
            raise AuditError("ledger audit must be an object")
        validate_audit(audit)
        archive_map = {item["pack"]: item["sha256"] for item in audit["archives"]}
        identity = (audit["sourceTag"], archive_map["base"], archive_map["extra"])
        if identity in identities:
            raise AuditError("duplicate exact audit identity")
        identities.add(identity)


def merge_ledger(existing: dict[str, Any] | None, audit: dict[str, Any]) -> dict[str, Any]:
    validate_audit(audit)
    if existing is None:
        ledger = {
            "schema": SCHEMA,
            "sourceRepository": SOURCE_REPOSITORY,
            "generatedAt": audit["generatedAt"],
            "audits": [],
        }
    else:
        ledger = json.loads(json.dumps(existing))
        validate_ledger(ledger)
    new_archives = {item["pack"]: item["sha256"] for item in audit["archives"]}
    existing_exact = next(
        (
            item
            for item in ledger["audits"]
            if item["sourceTag"] == audit["sourceTag"]
            and {value["pack"]: value["sha256"] for value in item["archives"]}
            == new_archives
        ),
        None,
    )
    if existing_exact is not None:
        if semantic_audit_payload(existing_exact) == semantic_audit_payload(audit):
            return ledger
    ledger["audits"] = [
        item
        for item in ledger["audits"]
        if not (
            item["sourceTag"] == audit["sourceTag"]
            and {value["pack"]: value["sha256"] for value in item["archives"]}
            == new_archives
        )
    ]
    ledger["audits"].append(audit)
    ledger["audits"].sort(key=lambda item: (item.get("sequence", 0), item["sourceTag"]))
    ledger["generatedAt"] = audit["generatedAt"]
    validate_ledger(ledger)
    return ledger


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    scan = subparsers.add_parser("scan", help="scan one exact Community Pack release")
    scan.add_argument("--repo", type=Path, default=Path.cwd())
    scan.add_argument("--implementation-repo", type=Path, default=Path.cwd())
    scan.add_argument("--registry", type=Path, required=True)
    scan.add_argument("--base-archive", type=Path, required=True)
    scan.add_argument("--extra-archive", type=Path, required=True)
    scan.add_argument("--base-sha256", required=True)
    scan.add_argument("--extra-sha256", required=True)
    scan.add_argument("--source-tag", required=True)
    scan.add_argument("--source-commit", required=True)
    scan.add_argument("--previous-source-commit", required=True)
    scan.add_argument("--release-url", required=True)
    scan.add_argument("--published-at", required=True)
    scan.add_argument("--api", required=True)
    scan.add_argument("--sequence", type=int, required=True)
    scan.add_argument("--issue-number", type=int)
    scan.add_argument("--issue-url")
    scan.add_argument("--decisions", type=Path)
    scan.add_argument("--author-heads", type=Path)
    scan.add_argument("--target-manifest", type=Path, action="append", required=True)
    scan.add_argument("--target-archive", type=Path, action="append", default=[])
    scan.add_argument("--generated-at")
    scan.add_argument("--output-audit", type=Path, required=True)
    scan.add_argument("--output-summary", type=Path, required=True)

    merge = subparsers.add_parser("merge", help="merge one audit into cumulative latest.json")
    merge.add_argument("--existing", type=Path)
    merge.add_argument("--audit", type=Path, required=True)
    merge.add_argument("--output", type=Path, required=True)

    identity = subparsers.add_parser(
        "semantic-sha256", help="print content identity excluding generatedAt"
    )
    identity.add_argument("audit", type=Path)

    validate = subparsers.add_parser("validate", help="validate a cumulative ledger")
    validate.add_argument("ledger", type=Path)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.command == "scan":
            audit, summary = audit_release(args)
            validate_audit(audit)
            write_json(args.output_audit, audit)
            args.output_summary.parent.mkdir(parents=True, exist_ok=True)
            args.output_summary.write_text(summary, encoding="utf-8")
            print(
                json.dumps(
                    {
                        "sourceTag": audit["sourceTag"],
                        "overallStatus": audit["overallStatus"],
                        "entries": len(audit["entries"]),
                        "unresolved": len(audit["unresolved"]),
                    },
                    separators=(",", ":"),
                )
            )
        elif args.command == "merge":
            existing = read_json(args.existing) if args.existing and args.existing.exists() else None
            audit = read_json(args.audit)
            write_json(args.output, merge_ledger(existing, audit))
        elif args.command == "validate":
            validate_ledger(read_json(args.ledger))
            print(f"validated {args.ledger}")
        elif args.command == "semantic-sha256":
            audit = read_json(args.audit)
            validate_audit(audit)
            print(semantic_audit_sha256(audit))
    except AuditError as error:
        print(f"protected app audit failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
