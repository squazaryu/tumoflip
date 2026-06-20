#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

try:
    from .apply_packages import PackageError, apply_packages
    from .validate_release import manifest_release_id, sha256
except ImportError:
    from apply_packages import PackageError, apply_packages
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

    def test_dry_run_does_not_change_sd(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, resources, sd = self.make_fixture(Path(directory))
            result = apply_packages(manifest, resources, sd, dry_run=True)
            self.assertEqual(result["verified"], 1)
            self.assertFalse((sd / "apps/ARF Tools/arf_status.fap").exists())

    def test_bad_source_hash_is_rejected_before_install(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest, resources, sd = self.make_fixture(Path(directory))
            (resources / "apps/ARF Tools/arf_status.fap").write_bytes(b"tampered")
            with self.assertRaises(PackageError):
                apply_packages(manifest, resources, sd)
            self.assertFalse((sd / "apps/ARF Tools/arf_status.fap").exists())


if __name__ == "__main__":
    unittest.main()
