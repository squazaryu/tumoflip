#!/usr/bin/env python3
"""Regression contracts for NFC supported-card plugin heap lifecycle."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = REPO_ROOT / "applications/main/nfc/helpers/nfc_supported_cards.c"


class NfcSupportedCardsHeapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_load_context_starts_without_a_stale_application(self) -> None:
        alloc = re.search(
            r"nfc_supported_cards_load_context_alloc\(void\).*?^}",
            self.source,
            flags=re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(alloc)
        self.assertIn("instance->app = NULL;", alloc.group(0))

    def test_plugin_unload_clears_the_pointer(self) -> None:
        unload = re.search(
            r"nfc_supported_cards_unload_plugin\(.*?^}",
            self.source,
            flags=re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(unload)
        self.assertIn("flipper_application_free(instance->app);", unload.group(0))
        self.assertIn("instance->app = NULL;", unload.group(0))

        self.assertIn(
            "nfc_supported_cards_unload_plugin(instance);\n\n    storage_dir_close",
            self.source,
        )

    def test_plugin_is_unloaded_before_persistent_cache_allocation(self) -> None:
        load_cache = re.search(
            r"void nfc_supported_cards_load_cache\(.*?^}",
            self.source,
            flags=re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(load_cache)
        body = load_cache.group(0)

        unload_index = body.index(
            "nfc_supported_cards_unload_plugin(instance->load_context);"
        )
        name_index = body.index(
            "plugin_cache.name = "
            "furi_string_alloc_set(instance->load_context->file_name);"
        )
        push_index = body.index("NfcSupportedCardsPluginCache_push_back")
        self.assertLess(unload_index, name_index)
        self.assertLess(name_index, push_index)


if __name__ == "__main__":
    unittest.main()
