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
        self.assertIn("test_subghz_protocol_packs.py", workflow)
        self.assertIn("test_update_splash.py", workflow)
        self.assertIn("tumoflip-packages.json", workflow)
        self.assertIn("tumoflip-packages.zip", workflow)
        self.assertIn("sha256sum", workflow)
        self.assertIn("gh release create", workflow)
        self.assertNotIn("gh release upload", workflow)
        self.assertNotIn("--clobber", workflow)
        self.assertIn("already exists; publish a new tag instead", workflow)
        self.assertIn('--repo "$GITHUB_REPOSITORY"', workflow)

    def test_package_release_workflow_supports_legacy_and_independent_catalogs(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/package-release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("target_release_tag:", workflow)
        self.assertIn("package_ref:", workflow)
        self.assertIn("catalog_release_tag:", workflow)
        self.assertIn("compatible_catalog_tag:", workflow)
        self.assertIn("fw-packages-dev-001", workflow)
        self.assertIn("already exists; publish a new revision instead", workflow)
        self.assertIn("package_catalog_baselines.json", workflow)
        self.assertIn('if [[ "$TAG" != "$EXPECTED_BASELINE" ]]', workflow)
        self.assertIn("Catalog $CATALOG_CHANNEL baseline must be", workflow)
        self.assertIn("compatible_manifest", workflow)
        self.assertIn("--target-source-commit", workflow)
        self.assertIn("--compatible-manifest", workflow)
        self.assertIn("--compatible-package-zip", workflow)
        self.assertIn("--compatible-release-tag", workflow)
        self.assertIn("Compatible catalog must precede", workflow)
        self.assertIn("steps.target.outputs.compatible_tag == ''", workflow)
        self.assertIn('CHANNEL="dev"', workflow)
        self.assertIn('if [[ "$MANIFEST_VERSION" != "$TAG" ]]', workflow)
        self.assertIn('CHANNEL: ${{ steps.target.outputs.channel }}', workflow)
        self.assertIn('if [[ "$CHANNEL" == "stable" ]]', workflow)
        self.assertIn("sync_readme_version.py", workflow)
        self.assertIn("heatshrink2==0.13.0", workflow)
        self.assertIn("package_release.py", workflow)
        self.assertIn("TOTP_CLI_PLUGIN_APP_IDS", workflow)
        self.assertIn('print(f"fap_{appid}")', workflow)
        self.assertIn("updater_package fap_esp_flasher fap_subghz_raw_edit", workflow)
        self.assertIn('"${TOTP_PLUGIN_TARGETS[@]}" -j2', workflow)
        self.assertIn("--target-manifest", workflow)
        self.assertIn("--target-package-zip", workflow)
        self.assertIn('steps.target.outputs.manifest', workflow)
        self.assertIn('steps.target.outputs.package_zip', workflow)
        self.assertIn('--pattern "tumoflip-packages.json"', workflow)
        self.assertIn('--pattern "tumoflip-packages.zip"', workflow)
        self.assertIn("test_readme_version_sync.py", workflow)
        self.assertIn("test_verify_release_assets.py", workflow)
        self.assertIn("verify_release_assets.py", workflow)
        self.assertIn('--pattern "${VERSION}-SHA256SUMS"', workflow)
        self.assertIn("--target-release-tag", workflow)
        self.assertIn("--catalog-release-tag", workflow)
        self.assertIn("tumoflip-packages.json", workflow)
        self.assertIn("tumoflip-packages.zip", workflow)
        self.assertIn("gh release download", workflow)
        self.assertIn("gh release upload", workflow)
        self.assertIn("--clobber", workflow)
        self.assertIn("gh release create", workflow)
        self.assertIn('--target "$SOURCE_COMMIT"', workflow)
        self.assertIn("This release contains no firmware image", workflow)
        self.assertIn("flipper-z-f7-full-${VERSION}.dfu", workflow)
        publish_step = workflow.split(
            "- name: Publish package-only assets",
            maxsplit=1,
        )[1]
        checksum_step = workflow.split(
            "- name: Prepare mixed firmware/package SHA-256 sums",
            maxsplit=1,
        )[1].split("- name: Publish package-only assets", maxsplit=1)[0]
        self.assertLess(
            checksum_step.index("verify_release_assets.py"),
            checksum_step.index('SHA_FILE="dist/f7-C/${VERSION}-SHA256SUMS"'),
        )
        self.assertNotIn("flipper-z-f7", publish_step)
        self.assertNotIn("updater_package", publish_step)
        self.assertIn('if [[ -n "$CATALOG_TAG" ]]', publish_step)

    def test_package_release_workflow_builds_a_verified_selective_delta(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/package-release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("base_catalog_tag:", workflow)
        self.assertIn("selective_overlays:", workflow)
        self.assertIn('BASE_TAG: ${{ github.event.inputs.base_catalog_tag }}', workflow)
        self.assertIn(
            'SELECTIVE_OVERLAYS: ${{ github.event.inputs.selective_overlays }}',
            workflow,
        )
        self.assertIn('--pattern "${BASE_TAG}-SHA256SUMS"', workflow)
        base_download = workflow.split(
            'if [[ -n "$BASE_TAG" ]]',
            maxsplit=1,
        )[1].split('echo "tag=$TAG"', maxsplit=1)[0]
        self.assertIn("verify_release_assets.py", base_download)
        self.assertIn('"$BASE_DIR/${BASE_TAG}-SHA256SUMS"', base_download)
        self.assertIn('"$BASE_DIR/tumoflip-packages.json"', base_download)
        self.assertIn('"$BASE_DIR/tumoflip-packages.zip"', base_download)
        self.assertIn('if [[ "$BASE_COMMIT" != "$BASE_SOURCE" ]]', workflow)
        self.assertIn("selective_catalog_overlay_names", workflow)
        self.assertIn("NORMALIZED_SELECTIVE_OVERLAYS", workflow)
        self.assertIn("package_catalog_lineage.json", (
            REPO_ROOT / "tools/tumoflip/package_release.py"
        ).read_text(encoding="utf-8"))
        self.assertIn("LATEST_LIVE_BASE", workflow)
        self.assertIn("max_by(.revision)", workflow)
        self.assertIn(".revision < $new_revision", workflow)
        self.assertIn("Base catalog must be latest live predecessor", workflow)
        self.assertIn("Pinned base catalog", workflow)
        self.assertNotIn(
            'echo "selective_overlays=$SELECTIVE_OVERLAYS" >> "$GITHUB_OUTPUT"',
            workflow,
        )
        self.assertIn(
            'echo "selective_overlays=$NORMALIZED_SELECTIVE_OVERLAYS" >> "$GITHUB_OUTPUT"',
            workflow,
        )
        self.assertIn("--base-manifest", workflow)
        self.assertIn("--base-package-zip", workflow)
        self.assertIn("--base-release-tag", workflow)
        self.assertIn("--base-source-commit", workflow)
        self.assertIn("--selective-overlays", workflow)

        selective_step = workflow.split(
            "- name: Build selective package resources",
            maxsplit=1,
        )[1].split("- name: Build full package resources", maxsplit=1)[0]
        self.assertIn("selective_catalog_build_targets", selective_step)
        self.assertIn('"${SELECTIVE_FBT_TARGETS[@]}" -j2', selective_step)
        self.assertNotIn("updater_package", selective_step)
        self.assertNotIn("fap_subghz_raw_edit", selective_step)
        self.assertNotIn("TOTP_CLI_PLUGIN", selective_step)

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
