#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import unittest

from tools.tumoflip.validate_stable_release import (
    aligned_stable_serial,
    parse_stable_serial,
    parse_stable_tag,
    validate_new_stable_release,
)

REPO_ROOT = Path(__file__).resolve().parents[2]


class ValidateStableReleaseTest(unittest.TestCase):
    def test_accepts_next_patch_and_serial(self) -> None:
        validate_new_stable_release(
            release_tag="v1.0.2",
            firmware_version="t-flppr-fw-002",
            previous_release_tag="v1.0.1",
            previous_firmware_version="t-flppr-fw-001",
        )

    def test_accepts_alignment_recovery_after_historical_mismatch(self) -> None:
        validate_new_stable_release(
            release_tag="v1.0.4",
            firmware_version="t-flppr-fw-004",
            previous_release_tag="v1.0.3",
            previous_firmware_version="t-flppr-fw-002",
        )

    def test_rejects_new_semver_line_without_an_explicit_mapping(self) -> None:
        with self.assertRaisesRegex(ValueError, "defined only for v1.0.N"):
            aligned_stable_serial("v1.1.0")

    def test_rejects_serial_overflow(self) -> None:
        with self.assertRaisesRegex(ValueError, "overflow"):
            aligned_stable_serial("v1.0.1000")

    def test_rejects_reused_tag(self) -> None:
        with self.assertRaises(ValueError):
            validate_new_stable_release(
                release_tag="v1.0.1",
                firmware_version="t-flppr-fw-002",
                previous_release_tag="v1.0.1",
                previous_firmware_version="t-flppr-fw-001",
            )

    def test_rejects_skipped_patch_on_same_line(self) -> None:
        with self.assertRaises(ValueError):
            validate_new_stable_release(
                release_tag="v1.0.3",
                firmware_version="t-flppr-fw-003",
                previous_release_tag="v1.0.1",
                previous_firmware_version="t-flppr-fw-001",
            )

    def test_accepts_patch_after_an_existing_consumed_tag(self) -> None:
        validate_new_stable_release(
            release_tag="v1.0.3",
            firmware_version="t-flppr-fw-003",
            previous_release_tag="v1.0.1",
            previous_firmware_version="t-flppr-fw-001",
            existing_release_tags={"v1.0.2"},
        )

    def test_requires_every_skipped_patch_tag_to_exist(self) -> None:
        with self.assertRaises(ValueError):
            validate_new_stable_release(
                release_tag="v1.0.4",
                firmware_version="t-flppr-fw-004",
                previous_release_tag="v1.0.1",
                previous_firmware_version="t-flppr-fw-001",
                existing_release_tags={"v1.0.2"},
            )

    def test_rejects_serial_that_does_not_match_patch(self) -> None:
        for version in ("t-flppr-fw-003", "t-flppr-fw-005"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate_new_stable_release(
                    release_tag="v1.0.4",
                    firmware_version=version,
                    previous_release_tag="v1.0.3",
                    previous_firmware_version="t-flppr-fw-002",
                )

    def test_rejects_non_monotonic_aligned_serial(self) -> None:
        with self.assertRaisesRegex(ValueError, "must advance beyond"):
            validate_new_stable_release(
                release_tag="v1.0.4",
                firmware_version="t-flppr-fw-004",
                previous_release_tag="v1.0.3",
                previous_firmware_version="t-flppr-fw-005",
            )

    def test_rejects_legacy_or_dev_firmware_identity(self) -> None:
        for version in ("t-flppr-fw-089-041", "t-dev-001-003"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                parse_stable_serial(version)

    def test_requires_three_component_stable_semver(self) -> None:
        for tag in ("v1.0", "v1.0.2-rc.1", "1.0.2"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                parse_stable_tag(tag)

    def test_cli_runs_from_repository_root(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                "tools/tumoflip/validate_stable_release.py",
                "--release-tag",
                "v1.0.4",
                "--firmware-version",
                "t-flppr-fw-004",
                "--previous-release-tag",
                "v1.0.3",
                "--previous-firmware-version",
                "t-flppr-fw-002",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "v1.0.3/t-flppr-fw-002 -> v1.0.4/t-flppr-fw-004",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
