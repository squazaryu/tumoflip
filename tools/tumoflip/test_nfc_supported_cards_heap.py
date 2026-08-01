#!/usr/bin/env python3
"""Regression contracts for the bounded NFC supported-card loader."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = REPO_ROOT / "applications/main/nfc/helpers/nfc_supported_cards.c"


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcSupportedCardsHeapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_loader_state_and_application_pointer_are_initialized(self) -> None:
        alloc = function_body(self.source, "nfc_supported_cards_alloc(")
        self.assertIn("instance->load_state = NfcSupportedCardsLoadStateIdle;", alloc)

        context_init = function_body(
            self.source, "nfc_supported_cards_load_context_init("
        )
        self.assertIn("instance->app = NULL;", context_init)

    def test_plugin_unload_clears_the_pointer(self) -> None:
        unload = function_body(self.source, "nfc_supported_cards_unload_plugin(")
        self.assertIn("flipper_application_free(instance->app);", unload)
        self.assertIn("instance->app = NULL;", unload)

        context_deinit = function_body(
            self.source, "nfc_supported_cards_load_context_deinit("
        )
        self.assertIn("nfc_supported_cards_unload_plugin(instance);", context_deinit)

    def test_cache_probe_never_maps_parser_sections(self) -> None:
        load_cache = function_body(self.source, "nfc_supported_cards_load_cache(")
        self.assertIn("storage_file_is_open(load_context.directory)", load_cache)
        self.assertNotIn("nfc_supported_cards_get_next_plugin", load_cache)
        self.assertNotIn("flipper_application_map_to_memory", load_cache)

        self.assertNotIn("NfcSupportedCardsPluginCache", self.source)
        self.assertNotIn("plugins_cache_arr", self.source)
        self.assertNotIn("ARRAY_DEF", self.source)

    def test_read_and_parse_stream_plugins_with_local_contexts(self) -> None:
        for signature, callback in (
            ("nfc_supported_cards_read(", "plugin->read"),
            ("nfc_supported_cards_parse(", "plugin->parse"),
        ):
            body = function_body(self.source, signature)
            self.assertIn(
                "NfcSupportedCardsLoadContext load_context;",
                body,
            )
            self.assertIn("nfc_supported_cards_load_context_init(&load_context);", body)
            self.assertIn(
                "nfc_supported_cards_get_next_plugin(&load_context, api_interface)", body
            )
            self.assertIn("plugin->protocol != protocol", body)
            self.assertIn(callback, body)
            self.assertIn("nfc_supported_cards_load_context_deinit(&load_context);", body)

    def test_parser_iteration_uses_no_temporary_heap_allocations(self) -> None:
        init = function_body(self.source, "nfc_supported_cards_load_context_init(")
        get_plugin = function_body(self.source, "nfc_supported_cards_get_plugin(")
        self.assertNotIn("malloc(", init)
        self.assertNotIn("furi_string_alloc", get_plugin)
        self.assertRegex(get_plugin, r"char plugin_path\s*\[")


if __name__ == "__main__":
    unittest.main()
