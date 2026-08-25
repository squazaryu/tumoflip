#!/usr/bin/env python3
"""Synchronize immutable FW Packages audit results with Tumoflip issues.

The FW Packages repository owns the immutable audit ledger.  This tool only
turns that ledger into a deterministic plan for the release-inventory issues
that live in this firmware repository.  It never changes source code, FAPs, or
release artifacts and deliberately does not reopen a closed issue.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping


ISSUE_TITLE = re.compile(
    r"^\[all-the-plugins\]\s+Review\s+([a-z0-9]+)\s+release inventory$"
)


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


def build_plan(ledger: Mapping[str, Any], issues: List[Mapping[str, Any]]) -> Dict[str, Any]:
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
    return {
        "schema": 1,
        "ledgerGeneratedAt": generated_at,
        "updates": updates,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", type=Path, required=True)
    parser.add_argument("--issues", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    ledger = json.loads(args.ledger.read_text(encoding="utf-8"))
    issues = json.loads(args.issues.read_text(encoding="utf-8"))
    plan = build_plan(ledger, issues)
    args.output.write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
