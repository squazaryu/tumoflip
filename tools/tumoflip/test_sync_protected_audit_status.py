#!/usr/bin/env python3

import hashlib
import json
import unittest

from tools.tumoflip.sync_protected_audit_status import (
    AUDIT_REPOSITORY,
    CHECKSUM_ASSET,
    LEDGER_ASSET,
    PROVENANCE_ASSET,
    build_plan,
    resolve_immutable_release,
    verify_immutable_bundle,
)


def audit(tag, sequence, status):
    return {
        "sourceTag": tag,
        "sequence": sequence,
        "overallStatus": status,
        "auditIssue": f"https://github.com/squazaryu/tumoflip-fw-packages/issues/{sequence}",
        "entries": [
            {"disposition": "auditedDifference"},
            {"disposition": "intentionallyReplaced"},
        ],
    }


class ProtectedAuditStatusTest(unittest.TestCase):
    def immutable_bundle(self):
        ledger = json.dumps(
            {"schema": 2, "generatedAt": "now", "audits": []},
            sort_keys=True,
        ).encode()
        tag = "audit-ledger-20260830-004"
        commit = "a" * 40
        provenance = json.dumps(
            {
                "schema": 1,
                "kind": "protectedAppAuditRelease",
                "auditReleaseTag": tag,
                "publisher": {"repository": AUDIT_REPOSITORY, "commit": commit},
                "auditSemanticSHA256": "b" * 64,
                "ledgerSHA256": hashlib.sha256(ledger).hexdigest(),
                "evidenceSHA256": "c" * 64,
            },
            sort_keys=True,
        ).encode()
        checksums = (
            f"{hashlib.sha256(ledger).hexdigest()}  {LEDGER_ASSET}\n"
            f"{hashlib.sha256(provenance).hexdigest()}  {PROVENANCE_ASSET}\n"
        ).encode()
        blobs = {
            LEDGER_ASSET: ledger,
            PROVENANCE_ASSET: provenance,
            CHECKSUM_ASSET: checksums,
        }
        assets = [
            {
                "id": index + 10,
                "name": name,
                "size": len(data),
                "digest": f"sha256:{hashlib.sha256(data).hexdigest()}",
            }
            for index, (name, data) in enumerate(blobs.items())
        ]
        api_release = {
            "id": 42,
            "tag_name": tag,
            "target_commitish": commit,
            "draft": False,
            "prerelease": False,
            "immutable": True,
            "published_at": "2026-08-31T20:54:22Z",
            "assets": assets,
        }
        release = resolve_immutable_release([[api_release]])
        tag_ref = {
            "ref": f"refs/tags/{tag}",
            "object": {"type": "commit", "sha": commit},
        }
        return api_release, release, tag_ref, blobs

    def test_exact_immutable_release_bundle_is_verified(self):
        _, release, tag_ref, blobs = self.immutable_bundle()
        ledger = verify_immutable_bundle(
            release,
            tag_ref,
            blobs[LEDGER_ASSET],
            blobs[PROVENANCE_ASSET],
            blobs[CHECKSUM_ASSET],
        )
        self.assertEqual(ledger["schema"], 2)

    def test_mutable_latest_audit_release_fails_closed(self):
        api_release, _, _, _ = self.immutable_bundle()
        api_release["immutable"] = False
        with self.assertRaisesRegex(ValueError, "not immutable"):
            resolve_immutable_release([[api_release]])

    def test_release_asset_digest_mismatch_fails_closed(self):
        _, release, tag_ref, blobs = self.immutable_bundle()
        tampered_ledger = b"!" + blobs[LEDGER_ASSET][1:]
        with self.assertRaisesRegex(ValueError, "asset digest mismatch"):
            verify_immutable_bundle(
                release,
                tag_ref,
                tampered_ledger,
                blobs[PROVENANCE_ASSET],
                blobs[CHECKSUM_ASSET],
            )

    def test_release_tag_target_mismatch_fails_closed(self):
        _, release, tag_ref, blobs = self.immutable_bundle()
        tag_ref["object"]["sha"] = "d" * 40
        with self.assertRaisesRegex(ValueError, "tag target differs"):
            verify_immutable_bundle(
                release,
                tag_ref,
                blobs[LEDGER_ASSET],
                blobs[PROVENANCE_ASSET],
                blobs[CHECKSUM_ASSET],
            )

    def test_provenance_publisher_mismatch_fails_closed(self):
        _, release, tag_ref, blobs = self.immutable_bundle()
        provenance = json.loads(blobs[PROVENANCE_ASSET])
        provenance["publisher"]["commit"] = "e" * 40
        tampered = json.dumps(provenance, sort_keys=True).encode()
        release["assets"][PROVENANCE_ASSET] = {
            **release["assets"][PROVENANCE_ASSET],
            "size": len(tampered),
            "sha256": hashlib.sha256(tampered).hexdigest(),
        }
        checksums = (
            f"{hashlib.sha256(blobs[LEDGER_ASSET]).hexdigest()}  {LEDGER_ASSET}\n"
            f"{hashlib.sha256(tampered).hexdigest()}  {PROVENANCE_ASSET}\n"
        ).encode()
        release["assets"][CHECKSUM_ASSET] = {
            **release["assets"][CHECKSUM_ASSET],
            "size": len(checksums),
            "sha256": hashlib.sha256(checksums).hexdigest(),
        }
        with self.assertRaisesRegex(ValueError, "publisher commit differs"):
            verify_immutable_bundle(
                release,
                tag_ref,
                blobs[LEDGER_ASSET],
                tampered,
                checksums,
            )

    def test_p2_is_grouped_with_the_base_release_and_stays_open_if_pending(self):
        ledger = {
            "schema": 2,
            "generatedAt": "2026-08-25T10:25:52+00:00",
            "audits": [
                audit("22aug2026", 1, "pending"),
                audit("22aug2026p2", 2, "verified"),
            ],
        }
        plan = build_plan(
            ledger,
            [{"number": 398, "title": "[all-the-plugins] Review 22aug2026 release inventory", "state": "OPEN"}],
        )

        update = plan["updates"][0]
        self.assertEqual(update["family"], "22aug2026")
        self.assertEqual(
            update["issueTitle"],
            "[all-the-plugins] Review 22aug2026 release inventory",
        )
        self.assertEqual(update["overallStatus"], "pending")
        self.assertFalse(update["shouldClose"])
        self.assertEqual(update["sourceTags"], ["22aug2026", "22aug2026p2"])

    def test_verified_release_family_closes_open_issue(self):
        plan = build_plan(
            {
                "schema": 2,
                "generatedAt": "now",
                "audits": [audit("18aug2026", 1, "verified"), audit("18aug2026p2", 2, "verified")],
            },
            [{"number": 365, "title": "[all-the-plugins] Review 18aug2026 release inventory", "state": "OPEN"}],
        )

        update = plan["updates"][0]
        self.assertEqual(update["overallStatus"], "verified")
        self.assertTrue(update["shouldClose"])
        self.assertIn("verified", update["marker"])

    def test_unrelated_issues_are_not_touched(self):
        plan = build_plan(
            {"schema": 2, "generatedAt": "now", "audits": [audit("25aug2026", 1, "verified")]},
            [{"number": 1, "title": "Unrelated work", "state": "OPEN"}],
        )
        self.assertEqual(plan["updates"], [])

    def test_closed_verified_issue_is_not_reopened_or_closed_again(self):
        plan = build_plan(
            {"schema": 2, "generatedAt": "now", "audits": [audit("25aug2026", 1, "verified")]},
            [{"number": 400, "title": "[all-the-plugins] Review 25aug2026 release inventory", "state": "CLOSED"}],
        )
        self.assertEqual(plan["updates"], [])


if __name__ == "__main__":
    unittest.main()
