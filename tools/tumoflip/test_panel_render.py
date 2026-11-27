"""Native draw callbacks, real font bounds, unknown/max fields and page motion."""
import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tumoflip.render_desktop_panel_native import render


class PanelRenderTests(unittest.TestCase):
    def test_all_pages_and_transition_edges(self):
        with tempfile.TemporaryDirectory(prefix="panel-ui-test-") as directory:
            try:
                paths = render(Path(directory))
            except subprocess.CalledProcessError as error:
                self.fail(error.stdout + error.stderr.decode() if isinstance(error.stderr, bytes)
                          else (error.stdout or "") + (error.stderr or ""))
            self.assertEqual(len(paths), 19)
            hashes = {p.stem: hashlib.sha256(p.read_bytes()).digest() for p in paths}
            self.assertEqual(len(set(hashes.values())), 18)  # final status equals the invalid-page fallback
            self.assertNotEqual(hashes["01-status"], hashes["03-status-unknown"])
            self.assertNotEqual(hashes["06-fault-unavailable"], hashes["07-last-fault"])


if __name__ == "__main__":
    unittest.main()
