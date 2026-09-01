#!/usr/bin/env python3
"""Synchronize immutable FW Packages audit results with Tumoflip issues.

The FW Packages repository owns the immutable audit ledger.  This tool only
turns that ledger into a deterministic plan for the release-inventory issues
that live in this firmware repository.  It never changes source code, FAPs, or
release artifacts and deliberately does not reopen a closed issue.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional


ISSUE_TITLE = re.compile(
    r"^\[all-the-plugins\]\s+Review\s+([a-z0-9]+)\s+release inventory$"
)
AUDIT_TAG = re.compile(r"^audit-ledger-(\d{8})-(\d{3})$")
HEX_40 = re.compile(r"^[0-9a-f]{40}$")
HEX_64 = re.compile(r"^[0-9a-f]{64}$")
AUDIT_REPOSITORY = "squazaryu/tumoflip-fw-packages"
LEDGER_ASSET = "protected-app-audit-ledger.json"
PROVENANCE_ASSET = "audit-provenance.json"
CHECKSUM_ASSET = "protected-app-audit-ledger-SHA256SUMS"
REQUIRED_ASSETS = (LEDGER_ASSET, PROVENANCE_ASSET, CHECKSUM_ASSET)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def resolve_immutable_release(release_pages: Any) -> Dict[str, Any]:
    """Resolve the newest immutable audit release to numeric asset identities."""

    if not isinstance(release_pages, list):
        raise ValueError("release API response must be an array")
    releases: List[Mapping[str, Any]] = []
    for page in release_pages:
        if isinstance(page, list):
            if not all(isinstance(item, Mapping) for item in page):
                raise ValueError("release API page contains a non-object")
            releases.extend(page)
        elif isinstance(page, Mapping):
            releases.append(page)
        else:
            raise ValueError("release API response contains an invalid page")

    candidates = []
    for release in releases:
        tag = str(release.get("tag_name", ""))
        match = AUDIT_TAG.fullmatch(tag)
        if match:
            candidates.append(((match.group(1), int(match.group(2))), release))
    if not candidates:
        raise ValueError("no protected audit release found")
    newest_key = max(key for key, _ in candidates)
    newest = [release for key, release in candidates if key == newest_key]
    if len(newest) != 1:
        raise ValueError("latest protected audit release identity is ambiguous")
    release = newest[0]

    if release.get("immutable") is not True:
        raise ValueError("latest protected audit release is not immutable")
    if release.get("draft") is not False or release.get("prerelease") is not False:
        raise ValueError("latest protected audit release is not public and final")
    release_id = release.get("id")
    if not isinstance(release_id, int) or isinstance(release_id, bool) or release_id < 1:
        raise ValueError("protected audit release ID is invalid")
    tag = str(release.get("tag_name", ""))
    target_commit = str(release.get("target_commitish", ""))
    if not HEX_40.fullmatch(target_commit):
        raise ValueError("protected audit release target is not an exact commit")

    raw_assets = release.get("assets")
    if not isinstance(raw_assets, list):
        raise ValueError("protected audit release assets are invalid")
    by_name: Dict[str, Mapping[str, Any]] = {}
    for asset in raw_assets:
        if not isinstance(asset, Mapping):
            raise ValueError("protected audit release contains an invalid asset")
        name = str(asset.get("name", ""))
        if name in REQUIRED_ASSETS:
            if name in by_name:
                raise ValueError(f"duplicate protected audit asset: {name}")
            by_name[name] = asset
    if set(by_name) != set(REQUIRED_ASSETS):
        raise ValueError("protected audit release is missing a required asset")

    assets: Dict[str, Dict[str, Any]] = {}
    for name in REQUIRED_ASSETS:
        asset = by_name[name]
        asset_id = asset.get("id")
        size = asset.get("size")
        digest = str(asset.get("digest", ""))
        if not isinstance(asset_id, int) or isinstance(asset_id, bool) or asset_id < 1:
            raise ValueError(f"invalid numeric asset ID for {name}")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            raise ValueError(f"invalid asset size for {name}")
        if not digest.startswith("sha256:") or not HEX_64.fullmatch(digest[7:]):
            raise ValueError(f"invalid GitHub asset digest for {name}")
        assets[name] = {"id": asset_id, "size": size, "sha256": digest[7:]}

    return {
        "schema": 1,
        "kind": "protectedAuditReleasePin",
        "repository": AUDIT_REPOSITORY,
        "releaseId": release_id,
        "tag": tag,
        "targetCommit": target_commit,
        "publishedAt": str(release.get("published_at", "")),
        "assets": assets,
    }


def verify_immutable_bundle(
    release: Mapping[str, Any],
    tag_ref: Mapping[str, Any],
    ledger_bytes: bytes,
    provenance_bytes: bytes,
    checksum_bytes: bytes,
) -> Dict[str, Any]:
    """Verify release, tag, API digests, checksums, and provenance as one unit."""

    if release.get("schema") != 1 or release.get("kind") != "protectedAuditReleasePin":
        raise ValueError("protected audit release pin identity is invalid")
    if release.get("repository") != AUDIT_REPOSITORY:
        raise ValueError("protected audit release repository is invalid")
    tag = str(release.get("tag", ""))
    target_commit = str(release.get("targetCommit", ""))
    if not AUDIT_TAG.fullmatch(tag) or not HEX_40.fullmatch(target_commit):
        raise ValueError("protected audit release tag or target is invalid")
    if tag_ref.get("ref") != f"refs/tags/{tag}":
        raise ValueError("protected audit tag ref identity is invalid")
    tag_object = tag_ref.get("object")
    if not isinstance(tag_object, Mapping) or tag_object.get("type") != "commit":
        raise ValueError("protected audit tag must point directly to a commit")
    if tag_object.get("sha") != target_commit:
        raise ValueError("protected audit tag target differs from the release pin")

    assets = release.get("assets")
    if not isinstance(assets, Mapping):
        raise ValueError("protected audit release pin assets are invalid")
    blobs = {
        LEDGER_ASSET: ledger_bytes,
        PROVENANCE_ASSET: provenance_bytes,
        CHECKSUM_ASSET: checksum_bytes,
    }
    computed: Dict[str, str] = {}
    for name, data in blobs.items():
        asset = assets.get(name)
        if not isinstance(asset, Mapping):
            raise ValueError(f"missing protected audit asset pin: {name}")
        if asset.get("size") != len(data):
            raise ValueError(f"protected audit asset size mismatch: {name}")
        digest = _sha256(data)
        if asset.get("sha256") != digest:
            raise ValueError(f"protected audit asset digest mismatch: {name}")
        computed[name] = digest

    try:
        checksum_text = checksum_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("protected audit checksums are not ASCII") from error
    checksums: Dict[str, str] = {}
    for line in checksum_text.splitlines():
        parts = line.split()
        if len(parts) != 2:
            raise ValueError("protected audit checksum line is invalid")
        digest, name = parts
        name = name.removeprefix("*")
        if name in checksums or not HEX_64.fullmatch(digest):
            raise ValueError("protected audit checksum entry is invalid")
        checksums[name] = digest
    expected_checksums = {
        LEDGER_ASSET: computed[LEDGER_ASSET],
        PROVENANCE_ASSET: computed[PROVENANCE_ASSET],
    }
    if checksums != expected_checksums:
        raise ValueError("protected audit checksum manifest differs from exact assets")

    try:
        provenance = json.loads(provenance_bytes)
        ledger = json.loads(ledger_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("protected audit JSON asset is invalid") from error
    if not isinstance(provenance, Mapping) or not isinstance(ledger, dict):
        raise ValueError("protected audit JSON root is invalid")
    if provenance.get("schema") != 1 or provenance.get("kind") != "protectedAppAuditRelease":
        raise ValueError("protected audit provenance identity is invalid")
    if provenance.get("auditReleaseTag") != tag:
        raise ValueError("protected audit provenance tag differs from release")
    publisher = provenance.get("publisher")
    if not isinstance(publisher, Mapping) or publisher.get("repository") != AUDIT_REPOSITORY:
        raise ValueError("protected audit publisher repository is invalid")
    if publisher.get("commit") != target_commit:
        raise ValueError("protected audit publisher commit differs from release target")
    if provenance.get("ledgerSHA256") != computed[LEDGER_ASSET]:
        raise ValueError("protected audit provenance ledger digest mismatch")
    for field in ("auditSemanticSHA256", "evidenceSHA256"):
        if not HEX_64.fullmatch(str(provenance.get(field, ""))):
            raise ValueError(f"protected audit provenance {field} is invalid")
    if ledger.get("schema") != 2 or not isinstance(ledger.get("audits"), list):
        raise ValueError("unsupported protected audit ledger schema")
    return ledger


def _family_tag(source_tag: str) -> str:
    """Treat a corrected ``22aug2026p2`` release as one audit family."""

    return source_tag[:-2] if source_tag.endswith("p2") else source_tag


def _latest_audits(audits: Iterable[Mapping[str, Any]]) -> Dict[str, Mapping[str, Any]]:
    latest: Dict[str, Mapping[str, Any]] = {}
    for audit in audits:
        tag = str(audit.get("sourceTag", "")).strip()
        if not tag:
            continue
        try:
            sequence = int(audit.get("sequence", 0))
        except (TypeError, ValueError):
            continue
        previous = latest.get(tag)
        if previous is None or sequence > int(previous.get("sequence", 0)):
            latest[tag] = audit
    return latest


def _entry_counts(audits: Iterable[Mapping[str, Any]]) -> Dict[str, int]:
    counts: Counter[str] = Counter()
    for audit in audits:
        for entry in audit.get("entries", []) or []:
            disposition = str(entry.get("disposition", "unknown"))
            counts[disposition] += 1
    return dict(sorted(counts.items()))


def _family_status(audits: List[Mapping[str, Any]]) -> str:
    statuses = {str(audit.get("overallStatus", "pending")) for audit in audits}
    if statuses == {"verified"}:
        return "verified"
    if "failed" in statuses or "needsReview" in statuses:
        return "needs-review"
    return "pending"


def _issue_families(issues: Iterable[Mapping[str, Any]]) -> Dict[str, Mapping[str, Any]]:
    result: Dict[str, Mapping[str, Any]] = {}
    for issue in issues:
        title = str(issue.get("title", ""))
        match = ISSUE_TITLE.fullmatch(title)
        if not match:
            continue
        family = _family_tag(match.group(1))
        # Prefer an open issue when duplicate historical issues exist.  The
        # issue number is a stable tie-breaker for deterministic output.
        previous = result.get(family)
        if previous is None:
            result[family] = issue
            continue
        current_open = str(issue.get("state", "")).lower() == "open"
        previous_open = str(previous.get("state", "")).lower() == "open"
        if (current_open and not previous_open) or (
            current_open == previous_open
            and int(issue.get("number", 0)) > int(previous.get("number", 0))
        ):
            result[family] = issue
    return result


def build_plan(
    ledger: Mapping[str, Any],
    issues: List[Mapping[str, Any]],
    release: Optional[Mapping[str, Any]] = None,
) -> Dict[str, Any]:
    """Return the idempotent issue update plan consumed by the workflow."""

    if ledger.get("schema") != 2:
        raise ValueError("unsupported protected audit ledger schema")
    audits = _latest_audits(ledger.get("audits", []) or [])
    families: Dict[str, List[Mapping[str, Any]]] = {}
    for audit in audits.values():
        tag = str(audit.get("sourceTag", "")).strip()
        families.setdefault(_family_tag(tag), []).append(audit)

    issue_by_family = _issue_families(issues)
    updates: List[Dict[str, Any]] = []
    generated_at = str(ledger.get("generatedAt", ""))
    for family, issue in sorted(issue_by_family.items()):
        # Closed historical inventory issues are already resolved.  Do not add
        # recurring bot comments or labels to them, and never reopen them if a
        # later ledger is corrected or backfilled.
        if str(issue.get("state", "")).lower() != "open":
            continue
        family_audits = sorted(families.get(family, []), key=lambda item: int(item.get("sequence", 0)))
        if not family_audits:
            continue
        status = _family_status(family_audits)
        sequence = max(int(audit.get("sequence", 0)) for audit in family_audits)
        source_tags = [str(audit.get("sourceTag")) for audit in family_audits]
        issue_number = int(issue["number"])
        marker = f"<!-- tumoflip-audit-sync:{family}:{sequence}:{status} -->"
        counts = _entry_counts(family_audits)
        count_lines = "\n".join(
            f"- `{name}`: {count}" for name, count in counts.items()
        ) or "- none"
        audit_links = sorted(
            {
                str(audit.get("auditIssue"))
                for audit in family_audits
                if audit.get("auditIssue")
            }
        )
        links = "\n".join(f"- {link}" for link in audit_links) or "- none"
        decision = (
            "The immutable ledger is verified for this release family. No FAP source port, binary copy, or firmware release is implied."
            if status == "verified"
            else "The ledger is not fully verified for this release family. Keep the issue open and do not promote a package or source change."
        )
        comment = (
            f"{marker}\n"
            "## Protected-app audit synchronized\n\n"
            f"- Release family: `{family}`\n"
            f"- Source tags: {', '.join(f'`{tag}`' for tag in source_tags)}\n"
            f"- Ledger generated: `{generated_at}`\n"
            f"- Status: **{status}**\n\n"
            "### Ledger dispositions\n"
            f"{count_lines}\n\n"
            "### Linked FW Packages audit issues\n"
            f"{links}\n\n"
            f"{decision}"
        )
        updates.append(
            {
                "issueNumber": issue_number,
                "issueTitle": str(issue.get("title", "")),
                "issueState": str(issue.get("state", "")).lower(),
                "family": family,
                "sourceTags": source_tags,
                "overallStatus": status,
                "sequence": sequence,
                "marker": marker,
                "comment": comment,
                "shouldClose": status == "verified"
                and str(issue.get("state", "")).lower() == "open",
            }
        )
    plan: Dict[str, Any] = {
        "schema": 1,
        "ledgerGeneratedAt": generated_at,
        "updates": updates,
    }
    if release is not None:
        plan["auditRelease"] = {
            "repository": release["repository"],
            "releaseId": release["releaseId"],
            "tag": release["tag"],
            "targetCommit": release["targetCommit"],
            "ledgerSHA256": release["assets"][LEDGER_ASSET]["sha256"],
        }
    return plan


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    resolve = subparsers.add_parser("resolve-release")
    resolve.add_argument("--releases", type=Path, required=True)
    resolve.add_argument("--output", type=Path, required=True)
    plan_parser = subparsers.add_parser("build-plan")
    plan_parser.add_argument("--release", type=Path, required=True)
    plan_parser.add_argument("--tag-ref", type=Path, required=True)
    plan_parser.add_argument("--ledger", type=Path, required=True)
    plan_parser.add_argument("--provenance", type=Path, required=True)
    plan_parser.add_argument("--checksums", type=Path, required=True)
    plan_parser.add_argument("--issues", type=Path, required=True)
    plan_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "resolve-release":
        releases = json.loads(args.releases.read_text(encoding="utf-8"))
        release = resolve_immutable_release(releases)
        args.output.write_text(
            json.dumps(release, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return 0

    release = json.loads(args.release.read_text(encoding="utf-8"))
    tag_ref = json.loads(args.tag_ref.read_text(encoding="utf-8"))
    ledger = verify_immutable_bundle(
        release,
        tag_ref,
        args.ledger.read_bytes(),
        args.provenance.read_bytes(),
        args.checksums.read_bytes(),
    )
    issues = json.loads(args.issues.read_text(encoding="utf-8"))
    plan = build_plan(ledger, issues, release)
    args.output.write_text(
        json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
