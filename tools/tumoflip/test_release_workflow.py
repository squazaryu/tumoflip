#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowTest(unittest.TestCase):
    def test_release_workflow_publishes_companion_packages(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("tags:", workflow)
        self.assertIn('"v*"', workflow)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("release_tag:", workflow)
        self.assertIn("sync_readme_version.py", workflow)
        self.assertIn("heatshrink2==0.13.0", workflow)
        self.assertIn("./fbt COMPACT=1 DEBUG=0", workflow)
        self.assertIn("updater_package fap_esp_flasher", workflow)
        self.assertIn("validate_release.py", workflow)
        self.assertIn("--write-manifest", workflow)
        self.assertIn("test_readme_version_sync.py", workflow)
        self.assertIn("validate_stable_release.py", workflow)
        self.assertIn("test_validate_stable_release.py", workflow)
        self.assertIn("test_subghz_drift.py", workflow)
        self.assertIn("test_subghz_protocol_capabilities.py", workflow)
        self.assertIn("test_subghz_protocol_packs.py", workflow)
        self.assertIn("test_subghz_raw_worker_startup.py", workflow)
        self.assertIn("test_subghz_raw_profiles.py", workflow)
        self.assertIn("test_hardware_acceptance_suite.py", workflow)
        self.assertIn("test_infrared_universal_save.py", workflow)
        self.assertIn("test_update_splash.py", workflow)
        self.assertIn("tumoflip-packages.json", workflow)
        self.assertIn("tumoflip-packages.zip", workflow)
        self.assertIn("sha256sum", workflow)
        self.assertIn("gh release create", workflow)
        self.assertNotIn("gh release upload", workflow)
        self.assertNotIn("--clobber", workflow)
        self.assertIn("already exists; publish a new tag instead", workflow)
        self.assertIn('--repo "$GITHUB_REPOSITORY"', workflow)

    def test_legacy_package_and_audit_workflows_are_retired(self) -> None:
        self.assertFalse(
            (REPO_ROOT / ".github/workflows/package-release.yml").exists()
        )
        self.assertFalse(
            (REPO_ROOT / ".github/workflows/protected-app-audit.yml").exists()
        )

    def test_release_validation_requires_four_kibibytes_of_c2_reserve(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("--min-c2-gap 4096", workflow)

    def test_pr_build_compiles_package_only_faps(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/pr-build.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("updater_package fap_esp_flasher", workflow)

    def test_subghz_architecture_is_documented_as_core_first(self) -> None:
        doc = (REPO_ROOT / "docs/subghz-architecture.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("core Sub-GHz in firmware", doc)
        self.assertIn("arf_subghz_standard.fap", doc)
        self.assertIn("must not be reintroduced", doc)
        self.assertIn("Sub-GHz` opens `ARF Sub-GHz Full`", doc)
        self.assertIn(
            "Standard Sub-GHz` inside `ARF Sub-GHz Full` opens the core firmware app",
            doc,
        )
        self.assertIn("Protocol Packs", doc)
        self.assertIn("subghz_drift_manifest.txt", doc)

    def test_hardware_validation_is_documented_as_manual(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        doc = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("### Hardware validation", workflow)
        self.assertIn("not run by CI", workflow)
        self.assertIn("CI must not mark hardware-only checks as passed", workflow)
        self.assertIn("docs/hardware-regression-checklist.md", workflow)

        self.assertIn("Checklist version: 2", doc)
        self.assertIn("Do not mark a hardware-only item as passed", doc)
        self.assertIn("Install And Identity", doc)
        self.assertIn("System Sub-GHz Internal CC1101", doc)
        self.assertIn("External CC1101 And Module One", doc)
        self.assertIn("Protocol Packs", doc)
        self.assertIn("BLE App Bridge And Runtime", doc)
        self.assertIn("Failure Report", doc)


if __name__ == "__main__":
    unittest.main()
