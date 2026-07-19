#!/usr/bin/env python3

import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "tools/tumoflip/subghz_drift_manifest.txt"


def manifest_entries() -> set[str]:
    return {
        line.strip()
        for line in MANIFEST.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    }


class SubGhzDriftTest(unittest.TestCase):
    def test_tracked_core_and_arf_files_are_synchronized(self) -> None:
        tracked_count = len(manifest_entries())
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
        self.assertIn(f"tracked={tracked_count}", result.stdout)
        self.assertIn(
            f"OK: {tracked_count} tracked Sub-GHz duplicate files match", result.stdout
        )

    def test_manifest_tracks_shared_surfaces_not_profile_forks(self) -> None:
        entries = manifest_entries()

        self.assertIn("helpers/subghz_chat.c", entries)
        self.assertIn("helpers/subghz_frequency_notebook.c", entries)
        self.assertIn("helpers/subghz_frequency_notebook.h", entries)
        self.assertIn("subghz_cli.c", entries)
        self.assertIn("scenes/subghz_scene_protocol_pack_info.c", entries)
        self.assertIn("views/subghz_read_raw.c", entries)
        self.assertIn("helpers/subghz_frequency_analyzer_worker.c", entries)
        self.assertIn("helpers/subghz_frequency_analyzer_worker.h", entries)
        self.assertIn("scenes/subghz_scene_frequency_analyzer.c", entries)
        self.assertIn("views/subghz_frequency_analyzer.c", entries)
        self.assertIn("views/subghz_frequency_analyzer.h", entries)
        self.assertNotIn("application.fam", entries)
        self.assertNotIn("subghz.c", entries)
        self.assertNotIn("helpers/subghz_txrx.c", entries)

    def test_hopper_plan_is_the_explicit_shared_api(self) -> None:
        helper = (REPO_ROOT / "lib/subghz/subghz_hopper_plan.h").read_text(
            encoding="utf-8"
        )
        core_txrx = (
            REPO_ROOT / "applications/main/subghz/helpers/subghz_txrx.c"
        ).read_text(encoding="utf-8")
        arf_txrx = (
            REPO_ROOT / "applications_user/arf_subghz_full/helpers/subghz_txrx.c"
        ).read_text(encoding="utf-8")

        self.assertIn("subghz_hopper_plan_next", helper)
        self.assertNotIn("furi_", helper)
        self.assertNotIn("subghz_devices_", helper)
        self.assertIn("<lib/subghz/subghz_hopper_plan.h>", core_txrx)
        self.assertIn("<lib/subghz/subghz_hopper_plan.h>", arf_txrx)
        self.assertIn("subghz_hopper_plan_next(", core_txrx)
        self.assertIn("subghz_hopper_plan_next(", arf_txrx)


if __name__ == "__main__":
    unittest.main()
