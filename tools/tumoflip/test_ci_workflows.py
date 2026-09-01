#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class CiWorkflowSecurityTests(unittest.TestCase):
    def test_pr_build_keeps_untrusted_code_read_only(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/pr-build.yml").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("pull-requests: write", workflow)
        self.assertNotIn("uses: actions/github-script", workflow)
        self.assertIn("HEAD_REF: ${{ github.head_ref }}", workflow)
        self.assertIn("REF_NAME: ${{ github.ref_name }}", workflow)
        self.assertIn("tr -c 'A-Za-z0-9._-'", workflow)
        self.assertNotIn('REF="${{ github.head_ref }}"', workflow)
        self.assertNotIn('REF="${{ github.ref_name }}"', workflow)
        self.assertIn("name: pr-report", workflow)
        self.assertIn("pr_number.txt", workflow)

    def test_privileged_commenter_never_executes_pr_code(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/pr-comment.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("workflow_run:", workflow)
        self.assertIn("actions: read", workflow)
        self.assertIn("pull-requests: write", workflow)
        self.assertNotIn("actions/checkout", workflow)
        self.assertIn("context.payload.workflow_run.head_sha", workflow)
        self.assertIn("pullRequest.head.sha !== headSha", workflow)
        self.assertIn("<!-- tumoflip-pr-report -->", workflow)

    def test_codeql_uses_supported_and_pinned_actions(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/codeql.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("actions/checkout@v6", workflow)
        self.assertIn("github/codeql-action/init@v4", workflow)
        self.assertIn("github/codeql-action/resolve-environment@v4", workflow)
        self.assertIn("github/codeql-action/analyze@v4", workflow)
        self.assertIn("github/codeql-action/upload-sarif@v4", workflow)
        self.assertIn(
            "advanced-security/filter-sarif@"
            "f3b8118a9349d88f7b1c0c488476411145b6270d",
            workflow,
        )

    def test_protected_audit_sync_verifies_immutable_release_before_writes(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/protected-audit-status-sync.yml"
        ).read_text(encoding="utf-8")

        self.assertNotIn("raw.githubusercontent.com", workflow)
        self.assertIn(
            "actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803",
            workflow,
        )
        self.assertIn("persist-credentials: false", workflow)
        self.assertIn("resolve-release", workflow)
        self.assertIn("releases/assets/$asset_id", workflow)
        self.assertIn("--tag-ref", workflow)
        self.assertIn("--provenance", workflow)
        self.assertIn("--checksums", workflow)
        self.assertIn(".title == $title and .state == \"open\"", workflow)

if __name__ == "__main__":
    unittest.main()
