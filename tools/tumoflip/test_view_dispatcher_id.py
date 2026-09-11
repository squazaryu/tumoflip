#!/usr/bin/env python3
"""Regression contract for the public ViewDispatcher ID availability check."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ViewDispatcherIdContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "applications/services/gui/view_dispatcher.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "applications/services/gui/view_dispatcher.c").read_text(
            encoding="utf-8"
        )
        cls.api_f7 = (ROOT / "targets/f7/api_symbols.csv").read_text(
            encoding="utf-8"
        )
        cls.api_f18 = (ROOT / "targets/f18/api_symbols.csv").read_text(
            encoding="utf-8"
        )

    def test_public_function_is_declared_and_exported_for_both_targets(self) -> None:
        self.assertIn(
            "bool view_dispatcher_check_id(ViewDispatcher* view_dispatcher, uint32_t view_id);",
            self.header,
        )
        expected = (
            "Function,+,view_dispatcher_check_id,_Bool,\"ViewDispatcher*, uint32_t\""
        )
        self.assertIn(expected, self.api_f7)
        self.assertIn(expected, self.api_f18)

    def test_check_is_non_mutating_and_uses_the_dispatcher_dictionary(self) -> None:
        match = re.search(
            r"bool view_dispatcher_check_id\(ViewDispatcher\* view_dispatcher, uint32_t view_id\)"
            r"\s*\{(?P<body>.*?)\n\}",
            self.source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("furi_check(view_dispatcher);", body)
        self.assertIn("ViewDict_get(view_dispatcher->views, view_id) == NULL", body)
        self.assertNotIn("ViewDict_set_at", body)
        self.assertNotIn("ViewDict_erase", body)

    def test_add_view_still_fails_closed_on_duplicate_ids(self) -> None:
        start = self.source.index(
            "void view_dispatcher_add_view(ViewDispatcher* view_dispatcher"
        )
        end = self.source.index("\n}", start) + 2
        add_view = self.source[start:end]
        self.assertIn(
            "furi_check(ViewDict_get(view_dispatcher->views, view_id) == NULL);",
            add_view,
        )


if __name__ == "__main__":
    unittest.main()
