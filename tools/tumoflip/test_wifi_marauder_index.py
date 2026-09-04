#!/usr/bin/env python3
"""Host regression test for Marauder Airtag/FindMy index validation."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/esp32_wifi_marauder"


class WifiMarauderIndexTest(unittest.TestCase):
    def test_decimal_device_index_parser(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("No host C compiler")

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "wifi_marauder_index_host_test"
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(APP_DIR),
                    str(APP_DIR / "wifi_marauder_index.c"),
                    str(REPO_ROOT / "tools/tumoflip/wifi_marauder_index_host_test.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )

            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )
            self.assertIn("wifi_marauder_index_host_test: PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
