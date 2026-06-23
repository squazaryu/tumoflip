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
        self.assertIn("./fbt COMPACT=1 DEBUG=0", workflow)
        self.assertIn("validate_release.py", workflow)
        self.assertIn("--write-manifest", workflow)
        self.assertIn("tumoflip-packages.json", workflow)
        self.assertIn("tumoflip-packages.zip", workflow)
        self.assertIn("sha256sum", workflow)
        self.assertIn("gh release create", workflow)
        self.assertIn("gh release upload", workflow)
        self.assertIn("--clobber", workflow)
        self.assertIn('--repo "$GITHUB_REPOSITORY"', workflow)

    def test_subghz_architecture_is_documented_as_core_first(self) -> None:
        doc = (REPO_ROOT / "docs/subghz-architecture.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("core Sub-GHz in firmware", doc)
        self.assertIn("arf_subghz_standard.fap", doc)
        self.assertIn("must not be reintroduced", doc)
        self.assertIn("Sub-GHz` opens the core firmware app", doc)
        self.assertIn("Protocol Packs", doc)

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

        self.assertIn("Checklist version: 1", doc)
        self.assertIn("Do not mark a hardware-only item as passed", doc)
        self.assertIn("Install And Identity", doc)
        self.assertIn("System Sub-GHz Internal CC1101", doc)
        self.assertIn("External CC1101 And Module One", doc)
        self.assertIn("Protocol Packs", doc)
        self.assertIn("BLE App Bridge And Runtime", doc)
        self.assertIn("Failure Report", doc)


if __name__ == "__main__":
    unittest.main()
