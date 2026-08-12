import unittest
import json
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class CiWorkflowSecurityTests(unittest.TestCase):
    def test_pr_build_keeps_untrusted_code_read_only(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/pr-build.yml"
        ).read_text(encoding="utf-8")

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
        workflow = (
            REPO_ROOT / ".github/workflows/pr-comment.yml"
        ).read_text(encoding="utf-8")

        self.assertIn("workflow_run:", workflow)
        self.assertIn("actions: read", workflow)
        self.assertIn("pull-requests: write", workflow)
        self.assertNotIn("actions/checkout", workflow)
        self.assertIn("context.payload.workflow_run.head_sha", workflow)
        self.assertIn("pullRequest.head.sha !== headSha", workflow)
        self.assertIn("<!-- tumoflip-pr-report -->", workflow)

    def test_codeql_uses_supported_and_pinned_actions(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/codeql.yml"
        ).read_text(encoding="utf-8")

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

    def test_protected_app_audit_serializes_partial_ledger_publication(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/protected-app-audit.yml"
        ).read_text(encoding="utf-8")

        self.assertIn("group: protected-app-audit-ledger", workflow)
        self.assertIn("contents: write", workflow)
        self.assertIn("issues: write", workflow)
        self.assertIn("Resolve exactly one canonical issue", workflow)
        self.assertIn("Publish cumulative partial ledger atomically", workflow)
        self.assertNotIn("Skip an already verified exact pack", workflow)
        self.assertIn("semantic-sha256", workflow)
        self.assertIn("$PAYLOAD_SHA.json", workflow)
        self.assertNotIn("if: steps.scan.outputs.status == 'verified'\n        shell: bash\n        env:\n          SOURCE_TAG", workflow)
        self.assertIn("Close only a fully verified canonical issue", workflow)
        self.assertIn("git -C \"$WORK\" rev-parse", workflow)
        self.assertIn("--implementation-repo \"$GITHUB_WORKSPACE\"", workflow)
        self.assertIn("sha256sum -c -", workflow)
        self.assertIn("protected-app-audit:$SOURCE_TAG:$BASE_SHA:$EXTRA_SHA", workflow)
        self.assertIn("-f tools/tumoflip/protected_audit_issue_lookup.jq", workflow)
        self.assertIn("repos/$GITHUB_REPOSITORY/releases?per_page=100", workflow)
        self.assertIn("^fw-packages-stable-[0-9]{3}$", workflow)
        self.assertIn("^fw-packages-dev-[0-9]{3}$", workflow)
        self.assertIn("(.draft == false) and (.prerelease == false)", workflow)
        self.assertIn("(.draft == false) and (.prerelease == true)", workflow)
        self.assertNotIn(
            "default: \"fw-packages-stable-001,fw-packages-dev-003\"",
            workflow,
        )

    def test_protected_app_audit_does_not_interpolate_inputs_in_shell(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/protected-app-audit.yml"
        ).read_text(encoding="utf-8")

        self.assertIn("SOURCE_TAG: ${{ inputs.source_tag }}", workflow)
        self.assertIn("DECISIONS_JSON: ${{ inputs.decisions_json }}", workflow)
        self.assertIn('"$DECISIONS_JSON" != *"/../"*', workflow)
        self.assertIn('"$DECISIONS_JSON" != *"/./"*', workflow)
        self.assertNotIn('SOURCE_TAG="${{ inputs.source_tag }}"', workflow)
        self.assertNotIn('DECISIONS_JSON="${{ inputs.decisions_json }}"', workflow)
        self.assertNotIn("pull_request_target", workflow)

    def test_target_discovery_selects_stable_release_and_dev_prerelease(self) -> None:
        releases = [
            {
                "tag_name": "fw-packages-stable-001",
                "draft": False,
                "prerelease": False,
            },
            {
                "tag_name": "fw-packages-dev-002",
                "draft": False,
                "prerelease": True,
            },
            {
                "tag_name": "fw-packages-dev-003",
                "draft": False,
                "prerelease": True,
            },
            {
                "tag_name": "fw-packages-dev-999",
                "draft": True,
                "prerelease": True,
            },
        ]
        jq_filter = r'''
          def revision: .tag_name | capture("-(?<revision>[0-9]{3})$").revision | tonumber;
          {
            stable: (map(select((.draft == false) and (.prerelease == false)
              and (.tag_name | test("^fw-packages-stable-[0-9]{3}$"))))
              | sort_by(revision) | reverse | .[0].tag_name),
            dev: (map(select((.draft == false) and (.prerelease == true)
              and (.tag_name | test("^fw-packages-dev-[0-9]{3}$"))))
              | sort_by(revision) | reverse | .[0].tag_name)
          }
        '''
        with tempfile.TemporaryDirectory() as temporary:
            fixture = Path(temporary) / "releases.json"
            fixture.write_text(json.dumps(releases), encoding="utf-8")
            result = subprocess.run(
                ["jq", "-c", jq_filter, str(fixture)],
                check=True,
                capture_output=True,
                text=True,
            )

        self.assertEqual(
            json.loads(result.stdout),
            {"stable": "fw-packages-stable-001", "dev": "fw-packages-dev-003"},
        )

    def test_protected_audit_issue_lookup_executes_exact_workflow_filter(self) -> None:
        title = "Audit protected apps for Community Pack 12aug2026"
        source = "585b144ac5b4d9a48a0e5a74570a6584353fdbba"
        base = "79e95fff98ba3afd95f5a31f81882412ed3505237e8fdae4f6e053328847e430"
        extra = "1cd57a12702343dbb5fcda6a4b37cbc10e80449fed9b67e04a19b5de5a87a1e0"
        marker = f"<!-- protected-app-audit:12aug2026:{base}:{extra} -->"
        issues = [
            {
                "number": 301,
                "title": title,
                "body": "Unrelated audit body",
            },
            {
                "number": 302,
                "title": title,
                "body": f"Legacy identity: {source} {base} {extra}",
            },
            {
                "number": 303,
                "title": title,
                "body": marker,
            },
            {
                "number": 304,
                "title": "Audit protected apps for Community Pack 9aug2026",
                "body": marker,
            },
        ]
        jq_filter = REPO_ROOT / "tools/tumoflip/protected_audit_issue_lookup.jq"
        with tempfile.TemporaryDirectory() as temporary:
            fixture = Path(temporary) / "issues.json"
            fixture.write_text(json.dumps(issues), encoding="utf-8")
            result = subprocess.run(
                [
                    "jq",
                    "-r",
                    "--arg",
                    "title",
                    title,
                    "--arg",
                    "marker",
                    marker,
                    "--arg",
                    "source",
                    source,
                    "--arg",
                    "base",
                    base,
                    "--arg",
                    "extra",
                    extra,
                    "-f",
                    str(jq_filter),
                    str(fixture),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.stdout.splitlines(), ["302", "303"])


if __name__ == "__main__":
    unittest.main()
