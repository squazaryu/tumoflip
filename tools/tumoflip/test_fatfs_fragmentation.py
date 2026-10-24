"""Execute the real FatFs implementation on a RAM-only fragmented volume."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FatFsFragmentationTests(unittest.TestCase):
    def test_fragmented_directories_reopen_append_truncate_and_fat_regressions(self):
        with tempfile.TemporaryDirectory(prefix="fatfs-ram-test-") as directory:
            work = Path(directory)
            # Actual target configuration, codecs and ff.c; no host filesystem hooks.
            (work / "ffconf.h").write_bytes((ROOT / "targets/f7/fatfs/ffconf.h").read_bytes())
            command = ["cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
                       "-ftrivial-auto-var-init=pattern", "-I", str(work),
                       "-I", str(ROOT / "lib/fatfs"),
                       str(ROOT / "lib/fatfs/ff.c"), str(ROOT / "lib/fatfs/option/unicode.c"),
                       str(ROOT / "tools/tumoflip/fatfs_fragmentation_host_test.c"),
                       "-o", str(work / "test")]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for volume in range(3):
                for mode in range(3):
                    with self.subTest(volume=volume, mode=mode):
                        run = subprocess.run([str(work / "test"), str(volume), str(mode)],
                                             capture_output=True, text=True, timeout=30)
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
