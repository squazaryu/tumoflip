"""Native draw callbacks, real font bounds, unknown/max fields and page motion."""
import hashlib
import csv
import subprocess
import tempfile
import unittest
from pathlib import Path
from PIL import Image

from tools.tumoflip.render_desktop_panel_native import render


class PanelRenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="panel-ui-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        try:
            cls.paths = render(cls.directory)
        except subprocess.CalledProcessError as error:
            raise AssertionError((error.stdout or "") + (error.stderr or "")) from error
        cls.trace = []
        with (cls.directory / "layout.tsv").open() as stream:
            for kind, frame, x, y, width, extra, detail in csv.reader(stream, delimiter="\t"):
                cls.trace.append(dict(kind=kind, frame=frame, x=int(x), y=int(y), width=int(width),
                                      extra=int(extra), detail=bytes.fromhex(detail).decode() if kind == "T" else int(detail)))

    def test_all_pages_and_transition_edges(self):
        self.assertEqual(len(self.paths), 24)
        hashes = {p.stem: hashlib.sha256(p.read_bytes()).digest() for p in self.paths}
        self.assertEqual(len(set(hashes.values())), 23)
        self.assertNotEqual(hashes["01-status"], hashes["03-status-unknown"])
        self.assertNotEqual(hashes["06-fault-unavailable"], hashes["07-last-fault"])

    def test_panel_footer_hints_are_removed_on_every_page(self):
        for path in self.paths:
            if path.stem.startswith(("12-", "13-", "14-", "15-", "16-")):
                continue
            with self.subTest(frame=path.stem):
                rows = [r for r in self.trace if r["frame"] == path.stem]
                self.assertFalse(any(r["kind"] == "T" and r["detail"] == "Apps" for r in rows))
                self.assertFalse(any(r["kind"] == "I" and (r["width"], r["extra"]) == (3, 5) for r in rows))
                with Image.open(path) as pixels:
                    self.assertEqual(set(pixels.crop((0, 63, 128, 64)).getdata()), {255})

    def test_panel_headings_are_lower_without_covering_the_status_bar(self):
        for path in self.paths[:9]:
            with self.subTest(frame=path.stem):
                text = [r for r in self.trace if r["frame"] == path.stem and r["kind"] == "T"]
                self.assertEqual([r["y"] for r in text[:2]], [24, 24])
                with Image.open(path) as pixels:
                    self.assertEqual(set(pixels.crop((0, 0, 128, 13)).getdata()), {255})

    def test_status_tiles_use_the_previous_footer_space(self):
        for frame in ("01-status", "02-status-no-card", "03-status-unknown", "09-invalid-page"):
            with self.subTest(frame=frame):
                boxes = [r for r in self.trace if r["frame"] == frame and r["kind"] == "R"]
                self.assertEqual([(r["x"], r["y"], r["width"], r["extra"]) for r in boxes],
                                 [(0, 29, 42, 34), (43, 29, 42, 34), (86, 29, 42, 34)])

    def test_idle_and_fault_content_use_the_full_height(self):
        for frame in ("04-idle-off", "05-idle-max", "06-fault-unavailable", "07-last-fault", "08-fault-long"):
            with self.subTest(frame=frame):
                text = [r for r in self.trace if r["frame"] == frame and r["kind"] == "T"]
                self.assertEqual([r["y"] for r in text], [24, 24, 37, 48, 59])

    def test_default_tile_values_share_the_primary_font_and_baseline(self):
        values = [r for r in self.trace if r["frame"] == "01-status" and r["kind"] == "T"
                  and r["detail"] in ("Ready", "On", "135K")]
        self.assertEqual(len(values), 3)
        self.assertEqual([(r["y"], r["extra"]) for r in values], [(42, 0)] * 3)
        self.assertTrue(all(r["width"] <= 36 for r in values))


if __name__ == "__main__":
    unittest.main()
