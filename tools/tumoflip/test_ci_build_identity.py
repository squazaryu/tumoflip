"""Pre-tag Stable CI must use the same identity as the release, not a short SHA."""
import importlib
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SHA = "0123456789abcdef0123456789abcdef01234567"


def identity(version, sha=SHA, ref="release-stable-011"):
    return importlib.import_module("tools.tumoflip.ci_build_identity").build_identity(version, sha, ref)


class CiBuildIdentityTests(unittest.TestCase):
    def test_stable_uses_exact_version_and_canonical_tag(self):
        self.assertEqual(identity("t-flppr-fw-011"), ("t-flppr-fw-011", "v1.0.11"))
        self.assertEqual(identity("t-flppr-fw-000"), ("t-flppr-fw-000", "v1.0.0"))
        self.assertEqual(identity("t-flppr-fw-999"), ("t-flppr-fw-999", "v1.0.999"))

    def test_development_keeps_diagnostic_sha_and_safe_ref(self):
        for version in ("t-dev-011-007", "t-dev-009-021-001"):
            with self.subTest(version=version):
                self.assertEqual(identity(version, ref="release-panel007-stable"),
                                 (SHA[:8], "release-panel007-stable"))

    def test_invalid_source_sha_is_rejected(self):
        for sha in ("", "12345678", "z" * 40, SHA + "\n"):
            with self.subTest(sha=sha):
                with self.assertRaises(ValueError):
                    identity("t-flppr-fw-011", sha=sha)

    def test_unknown_or_legacy_stable_identity_fails_closed(self):
        for version in ("unknown", "t-flppr-fw-011-001", "tmwhflpprarf009-001", "t-flppr-fw-011\n"):
            with self.subTest(version=version):
                with self.assertRaises(ValueError):
                    identity(version)

    def test_unsanitized_workflow_ref_cannot_inject_outputs(self):
        for ref in ("", "ref\nworkflow_ref=other", "refs/heads/dev", "$(bad)", "x" * 201):
            with self.subTest(ref=ref):
                with self.assertRaises(ValueError):
                    identity("t-dev-011-007", ref=ref)

    def test_cli_emits_literal_outputs_and_errors_have_no_outputs(self):
        command = [sys.executable, str(ROOT / "tools/tumoflip/ci_build_identity.py"),
                   "--version", "t-flppr-fw-011", "--sha", SHA, "--ref", "release-stable-011"]
        result = subprocess.run(command, text=True, capture_output=True, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "firmware_dist=t-flppr-fw-011\nworkflow_ref=v1.0.11\n")
        command[-1] = "bad\nextra=value"
        result = subprocess.run(command, text=True, capture_output=True, cwd=ROOT)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_pr_workflow_build_consumes_the_checked_identity(self):
        workflow = (ROOT / ".github/workflows/pr-build.yml").read_text()
        self.assertIn("python3 tools/tumoflip/ci_build_identity.py", workflow)
        build = workflow.split("- name: Build firmware (minimal / clean)", 1)[1].split("- name:", 1)[0]
        self.assertIn("DIST_SUFFIX: ${{ steps.vars.outputs.firmware_dist }}", build)
        self.assertIn("WORKFLOW_BRANCH_OR_TAG: ${{ steps.vars.outputs.workflow_ref }}", build)
        self.assertNotIn("DIST_SUFFIX: ${{ steps.vars.outputs.sha8 }}", build)

    def test_main_reads_options_and_rejects_bad_inputs_without_output(self):
        module = importlib.import_module("tools.tumoflip.ci_build_identity")
        arguments = ["identity", "--sha", SHA, "--ref", "main"]
        out, err = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", arguments), patch.object(module, "read_dist_suffix", return_value="t-flppr-fw-011"):
            with redirect_stdout(out), redirect_stderr(err):
                self.assertEqual(module.main(), 0)
        self.assertEqual(out.getvalue(), "firmware_dist=t-flppr-fw-011\nworkflow_ref=v1.0.11\n")
        self.assertEqual(err.getvalue(), "")
        out, err = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", arguments + ["--version", "bad"]):
            with redirect_stdout(out), redirect_stderr(err):
                self.assertEqual(module.main(), 2)
        self.assertEqual(out.getvalue(), "")
        self.assertIn("error:", err.getvalue())


if __name__ == "__main__":
    unittest.main()
