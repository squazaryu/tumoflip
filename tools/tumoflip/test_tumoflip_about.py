#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
ABOUT_SOURCE = REPO_ROOT / "applications/settings/about/about.c"


class TumoflipAboutTest(unittest.TestCase):
    def test_about_uses_standalone_tumoflip_identity(self) -> None:
        source = ABOUT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('"Independent firmware\\n"', source)
        self.assertIn('"Stable release\\n"', source)
        self.assertIn('"github.com/squazaryu\\n"', source)
        self.assertIn('"/tumoflip"', source)
        self.assertIn('"FW Packages are published\\n"', source)
        self.assertNotIn("Unleashed", source)
        self.assertNotIn("DarkFlippers", source)


if __name__ == "__main__":
    unittest.main()
