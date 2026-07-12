#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumokey_phase_a"


class TumoKeyPhaseATest(unittest.TestCase):
    def test_phase_a_is_explicitly_bounded_and_packaged(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        readme = (APP_ROOT / "README.md").read_text(encoding="utf-8")
        app = (APP_ROOT / "tumokey_app.c").read_text(encoding="utf-8")

        self.assertIn('appid="tumokey_phase_a"', manifest)
        self.assertIn('fap_category="Module One/Labs"', manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Labs/tumokey_phase_a.fap"',
            manifest,
        )
        self.assertIn("EXPERIMENTAL", app)
        self.assertIn("does not create, store, import", readme.lower())
        self.assertNotIn("furi_check(", app)
        self.assertNotIn("furi_crash(", app)

    def test_portable_core_with_sanitizers(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "tumokey_phase_a_host_test"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    str(REPO_ROOT / "applications_user/tumokey_phase_a/tumokey_core.c"),
                    str(REPO_ROOT / "tools/tumoflip/tumokey_phase_a_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)

            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("tumokey_phase_a_host_test: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
