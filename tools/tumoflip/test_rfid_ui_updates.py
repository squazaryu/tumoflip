from pathlib import Path
import tempfile
import unittest
from tools.tumoflip.render_rfid_updates import render

ROOT = Path(__file__).resolve().parents[2]


class RfidUiUpdatesTests(unittest.TestCase):
    def test_popup_width_is_opt_in_and_exported(self):
        self.assertIn("popup_set_text_width", (ROOT / "applications/services/gui/modules/popup.h").read_text())
        self.assertIn("Function,+,popup_set_text_width", (ROOT / "targets/f7/api_symbols.csv").read_text())

    def test_native_read_write_and_ibutton_screens_stay_in_bounds(self):
        with tempfile.TemporaryDirectory(prefix="rfid-ui-test-") as directory:
            self.assertTrue(render(directory).is_file())


if __name__ == "__main__":
    unittest.main()
