#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

try:
    from .apply_packages import PACKAGE_STATE_FILE, PackageError, apply_packages
    from .validate_release import manifest_release_id, sha256
except ImportError:
    from apply_packages import PACKAGE_STATE_FILE, PackageError, apply_packages
    from validate_release import manifest_release_id, sha256


class ApplyPackagesTest(unittest.TestCase):
    def make_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        resources = root / "resources"
        sd = root / "sd"
        source = resources / "apps/ARF Tools/arf_status.fap"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"new app")
        sd.mkdir()
        legacy = sd / "apps/ARF Tools/ARF Status.fap"
        legacy.parent.mkdir(parents=True)
        legacy.write_bytes(b"old app")
        manifest = {
            "schema": 2,
            "firmware": {
                "version": "t-dev-089-035-001",
                "api": "87.17",
            },
            "package_release": {
                "id": "test-package",
            },
            "packages": {
                "arf": [
                    {
                        "source": "apps/ARF Tools/arf_status.fap",
                        "target": "/ext/apps/ARF Tools/arf_status.fap",
                        "bytes": source.stat().st_size,
                        "sha256": sha256(source),
                    }
                ]
            },
            "cleanup": [
                {
                    "legacy": "/ext/apps/ARF Tools/ARF Status.fap",
                    "canonical": "/ext/apps/ARF Tools/arf_status.fap",
                }
            ],
        }
        manifest["release_id"] = manifest_release_id(manifest)
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path, resources, sd

    def test_apply_installs_and_cleans_legacy_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, resources, sd = self.make_fixture(Path(directory))
            state = apply_packages(manifest, resources, sd)
            self.assertEqual(
                (sd / "apps/ARF Tools/arf_status.fap").read_bytes(), b"new app"
            )
            self.assertFalse((sd / "apps/ARF Tools/ARF Status.fap").exists())
            self.assertEqual(state["groups"], ["arf"])
            self.assertTrue((sd / ".tumoflip/install-state.json").is_file())
            package_state = sd / ".tumoflip" / PACKAGE_STATE_FILE
            self.assertTrue(package_state.is_file())
            state_text = package_state.read_text(encoding="utf-8")
            for expected in (
                "Filetype: Tumoflip Package State",
                "Version: 1",
                "Schema: 2",
                f"ReleaseId: {state['release_id']}",
                f"Transaction: {state['transaction']}",
                "Firmware: t-dev-089-035-001",
                "FirmwareApi: 87.17",
                "PackageRelease: test-package",
                "Groups: arf",
                "InstalledFiles: 1",
                "CleanupCandidates: 1",
                f"Rollback: {state['rollback']}",
            ):
                self.assertIn(expected, state_text)

    def test_dry_run_does_not_change_sd(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, resources, sd = self.make_fixture(Path(directory))
            result = apply_packages(manifest, resources, sd, dry_run=True)
            self.assertEqual(result["verified"], 1)
            self.assertFalse((sd / "apps/ARF Tools/arf_status.fap").exists())
            self.assertFalse((sd / ".tumoflip" / PACKAGE_STATE_FILE).exists())

    def test_bad_source_hash_is_rejected_before_install(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, resources, sd = self.make_fixture(Path(directory))
            (resources / "apps/ARF Tools/arf_status.fap").write_bytes(b"tampered")
            with self.assertRaises(PackageError):
                apply_packages(manifest, resources, sd)
            self.assertFalse((sd / "apps/ARF Tools/arf_status.fap").exists())


if __name__ == "__main__":
    unittest.main()
