#!/usr/bin/env python3

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class Cc1101IntermediateFrequencyTest(unittest.TestCase):
    def test_writes_if_word_to_fsctrl1(self) -> None:
        compiler = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "cc1101_if_register_test"
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools/tumoflip/cc1101_host_mocks"),
                    "-I",
                    str(ROOT / "lib/drivers"),
                    str(ROOT / "lib/drivers/cc1101.c"),
                    str(ROOT / "tools/tumoflip/cc1101_if_register_host_test.c"),
                    "-o",
                    str(binary),
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
                [str(binary)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )


if __name__ == "__main__":
    unittest.main()
