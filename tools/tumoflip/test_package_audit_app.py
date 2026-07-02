#!/usr/bin/env python3

import unittest
from pathlib import Path

try:
    from .validate_release import ARF_LEGACY_PATHS
except ImportError:
    from validate_release import ARF_LEGACY_PATHS


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumoflip_packages"


class PackageAuditAppTest(unittest.TestCase):
    def test_package_audit_app_is_packaged_as_base_tool(self) -> None:
        validate_release = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("apps/Tools/tumoflip_packages.fap", validate_release)

    def test_package_audit_app_is_read_only_and_hashes_files(self) -> None:
        source = (APP_ROOT / "tumoflip_packages.c").read_text(encoding="utf-8")
        for expected in (
            "TUMO_PACKAGE_STATE_PATH",
            "TUMO_INSTALL_STATE_PATH",
            "TUMO_MAX_AUDIT_FILES",
            "tumo_sha256_update",
            "storage_common_stat",
            "Read-only. No cleanup is performed.",
        ):
            self.assertIn(expected, source)
        for forbidden in ("storage_common_remove", "FSAM_WRITE", "FSOM_CREATE"):
            self.assertNotIn(forbidden, source)

    def test_package_audit_app_explains_firmware_only_state(self) -> None:
        source = (APP_ROOT / "tumoflip_packages.c").read_text(encoding="utf-8")
        for expected in (
            "Overall: no package metadata",
            "State: not recorded",
            "normal after a firmware-only self-update",
            "Apply the Tumoflip FW Package bundle once",
        ):
            self.assertIn(expected, source)

    def test_known_release_cleanup_paths_are_visible_in_audit(self) -> None:
        source = (APP_ROOT / "tumoflip_packages.c").read_text(encoding="utf-8")
        for legacy, canonical in ARF_LEGACY_PATHS.items():
            self.assertIn(legacy, source)
            self.assertIn(canonical, source)


if __name__ == "__main__":
    unittest.main()
