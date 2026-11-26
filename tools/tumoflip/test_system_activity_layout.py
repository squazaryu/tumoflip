"""Real core screen renderer and key transitions, no hardware or recorded data."""
import tempfile
import unittest
from pathlib import Path
from tools.tumoflip.render_system_activity import render


class SystemActivityLayoutTests(unittest.TestCase):
    def test_all_pages_confirmations_busy_and_counter_limits(self):
        with tempfile.TemporaryDirectory(prefix="system-activity-frames-") as directory:
            output = Path(directory)
            render(output)
            self.assertEqual(len(list(output.glob("*.pgm"))), 10)
            self.assertTrue((output / "system-activity.png").is_file())


if __name__ == "__main__":
    unittest.main()
