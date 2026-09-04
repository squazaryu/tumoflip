#!/usr/bin/env python3
"""Host fault-injection regression for RAW worker startup handshakes."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
STUBS = REPO_ROOT / "tools/tumoflip/fixtures/subghz_raw_worker_stubs"


class SubGhzRawWorkerStartupTest(unittest.TestCase):
    def test_reopen_failure_and_normal_eof_lifecycle(self) -> None:
        compiler = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "subghz_raw_worker_startup_host_test"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    "-I",
                    str(STUBS),
                    "-I",
                    str(REPO_ROOT),
                    str(REPO_ROOT / "lib/subghz/subghz_file_encoder_worker.c"),
                    str(
                        REPO_ROOT
                        / "tools/tumoflip/subghz_raw_worker_startup_host_test.c"
                    ),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

            run = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=2,
                check=False,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
