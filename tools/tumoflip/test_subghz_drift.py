#!/usr/bin/env python3

import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "tools/tumoflip/subghz_drift_manifest.txt"


class SubGhzDriftTest(unittest.TestCase):
    def test_tracked_core_and_arf_files_are_synchronized(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                "tools/tumoflip/check_subghz_drift.py",
                "--repo-root",
                str(REPO_ROOT),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("tracked=29", result.stdout)
        self.assertIn("OK: 29 tracked Sub-GHz duplicate files match", result.stdout)

    def test_manifest_tracks_shared_surfaces_not_profile_forks(self) -> None:
        entries = {
            line.strip()
            for line in MANIFEST.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")
        }

        self.assertIn("subghz_cli.c", entries)
        self.assertIn("scenes/subghz_scene_protocol_pack_info.c", entries)
        self.assertIn("views/subghz_read_raw.c", entries)
        self.assertNotIn("application.fam", entries)
        self.assertNotIn("subghz.c", entries)
        self.assertNotIn("helpers/subghz_txrx.c", entries)


if __name__ == "__main__":
    unittest.main()
