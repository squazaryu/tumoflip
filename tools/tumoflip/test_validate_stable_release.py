#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import unittest

from tools.tumoflip.validate_stable_release import (
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

    def test_accepts_new_minor_with_next_serial(self) -> None:
        validate_new_stable_release(
            release_tag="v1.1.0",
            firmware_version="t-flppr-fw-002",
            previous_release_tag="v1.0.1",
            previous_firmware_version="t-flppr-fw-001",
        )

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
                firmware_version="t-flppr-fw-002",
                previous_release_tag="v1.0.1",
                previous_firmware_version="t-flppr-fw-001",
            )

    def test_rejects_reused_or_skipped_serial(self) -> None:
        for version in ("t-flppr-fw-001", "t-flppr-fw-003"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate_new_stable_release(
                    release_tag="v1.0.2",
                    firmware_version=version,
                    previous_release_tag="v1.0.1",
                    previous_firmware_version="t-flppr-fw-001",
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
                "v1.0.2",
                "--firmware-version",
                "t-flppr-fw-002",
                "--previous-release-tag",
                "v1.0.1",
                "--previous-firmware-version",
                "t-flppr-fw-001",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "v1.0.1/t-flppr-fw-001 -> v1.0.2/t-flppr-fw-002",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
