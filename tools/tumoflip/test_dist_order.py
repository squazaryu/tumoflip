"""Run the actual dist registration/install graph with SCons, not a mock scheduler."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/tumoflip/fixtures/dist_order/SConstruct"


class DistOrderTests(unittest.TestCase):
    def run_graph(self, targets, *, fail_validator=False, empty=False):
        with tempfile.TemporaryDirectory(prefix="tumoflip-dist-order-") as directory:
            result = subprocess.run(
                [sys.executable, "-m", "SCons", "-Q", "-j2", "-f", str(FIXTURE), *targets],
                cwd=directory, capture_output=True, text=True, timeout=20,
                env={**os.environ, "TUMOFLIP_DIST_TEST_ROOT": str(ROOT),
                     "TUMOFLIP_DIST_TEST_FAIL_VALIDATOR": "1" if fail_validator else "0",
                     "TUMOFLIP_DIST_TEST_EMPTY": "1" if empty else "0"},
            )
            root = Path(directory)
            events = (root / "dist-events.txt").read_text().splitlines() if (root / "dist-events.txt").exists() else []
            return {
                "returncode": result.returncode, "output": result.stdout + result.stderr,
                "fap": (root / "dist/f7-C/apps/Tools/app.fap").read_bytes() if (root / "dist/f7-C/apps/Tools/app.fap").exists() else None,
                "debug": (root / "dist/f7-C/debug_elf/app.elf").is_file(),
                "firmware": (root / "dist/f7-C/firmware.bin").is_file(), "events": events,
            }

    def test_combined_distribution_preserves_fap_and_debug_in_both_cli_orders(self):
        for targets in (["fap_dist", "updater_package"], ["updater_package", "fap_dist"]):
            with self.subTest(targets=targets):
                result = self.run_graph(targets)
                self.assertEqual(result["returncode"], 0, result["output"])
                self.assertEqual(result["fap"], b"FAP fixture", result["output"])
                self.assertTrue(result["debug"], result["output"])
                self.assertEqual(result["events"], ["updater_package"])

    def test_direct_dist_node_preserves_apps(self):
        result = self.run_graph(["dist_updater_package", "fap_dist"])
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["fap"], b"FAP fixture", result["output"])

    def test_all_selected_dist_commands_complete_before_install(self):
        result = self.run_graph(["fap_dist", "fw_dist", "updater_minpackage"])
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(set(result["events"]), {"fw_dist", "updater_minpackage"})
        self.assertEqual(result["fap"], b"FAP fixture", result["output"])
        self.assertTrue(result["debug"], result["output"])

    def test_apps_only_does_not_pull_unselected_distribution(self):
        result = self.run_graph(["fap_dist"])
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["events"], [])
        self.assertEqual(result["fap"], b"FAP fixture")
        self.assertFalse(result["firmware"])

    def test_distribution_only_does_not_install_apps(self):
        result = self.run_graph(["updater_package"])
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["events"], ["updater_package"])
        self.assertIsNone(result["fap"])

    def test_validator_failure_is_not_bypassed(self):
        result = self.run_graph(["fap_dist", "updater_package"], fail_validator=True)
        self.assertNotEqual(result["returncode"], 0)
        self.assertIn("FIXTURE VALIDATOR FAILED", result["output"])
        self.assertIsNone(result["fap"])

    def test_empty_app_set_keeps_selected_firmware_distribution(self):
        result = self.run_graph(["updater_package", "fap_dist"], empty=True)
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["events"], ["updater_package"])
        self.assertIsNone(result["fap"])


if __name__ == "__main__":
    unittest.main()
