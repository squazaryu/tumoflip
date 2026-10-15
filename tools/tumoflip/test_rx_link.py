"""Bounded passive Module One wire/backend tests; no device or radio activity."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/tumo_rx_link"


class RxLinkTests(unittest.TestCase):
    def test_wire_and_backend(self):
        with tempfile.TemporaryDirectory(prefix="tumo-rx-link-") as directory:
            executable = Path(directory) / "rx_link_test"
            source = APP / "rx_link.c"
            # An absent implementation intentionally produces undefined references.
            production = [str(source)] if source.exists() else []
            built = subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(APP),
                str(ROOT / "tools/tumoflip/fixtures/rx_link_test.c"), *production,
                "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
