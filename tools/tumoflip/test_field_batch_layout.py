"""Actual 128x64 custom pages and stock text scrolling, including error states."""
from pathlib import Path
import tempfile
import unittest
from tools.tumoflip.render_field_batch import render, EXPECTED_FRAMES


class FieldBatchLayoutTests(unittest.TestCase):
    def test_production_field_batch_pages_fit_without_overlapping_text(self):
        with tempfile.TemporaryDirectory(prefix='field-batch-layout-') as directory:
            self.assertEqual(render(Path(directory)), EXPECTED_FRAMES)


if __name__ == '__main__': unittest.main()
