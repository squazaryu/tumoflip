#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SUBMENU_SOURCE = REPO_ROOT / "applications/services/gui/modules/submenu.c"


class SubmenuExtendedEventsTest(unittest.TestCase):
    def test_submenu_item_copy_preserves_extended_event_flag(self) -> None:
        source = SUBMENU_SOURCE.read_text(encoding="utf-8")

        self.assertIn("item->has_extended_events = false;", source)
        self.assertGreaterEqual(source.count("item->has_extended_events = src->has_extended_events;"), 2)
        self.assertIn("item->has_extended_events = true;", source)
        self.assertIn("item->callback_ex(item->callback_context, input_type, item->index);", source)


if __name__ == "__main__":
    unittest.main()
