#!/usr/bin/env python3

import unittest

from tools.tumoflip.sync_protected_audit_status import build_plan


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
