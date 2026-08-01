#!/usr/bin/env python3
"""Regression contracts for NFC protocol-support FAL heap safety."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c"
)


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcProtocolSupportHeapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_protocol_support_has_a_fail_closed_default(self) -> None:
        alloc = function_body(self.source, "void nfc_protocol_support_alloc(")
        self.assertIn("memset(protocol_support, 0, sizeof(NfcProtocolSupport));", alloc)
        self.assertIn("protocol_support->base = &nfc_protocol_support_empty;", alloc)
        self.assertIn(
            "if(protocol_support->base == &nfc_protocol_support_empty)", alloc
        )
        self.assertIn("protocol_support->plugin_manager = NULL;", alloc)

    def test_protocol_plugin_path_does_not_allocate_heap_memory(self) -> None:
        alloc = function_body(self.source, "void nfc_protocol_support_alloc(")
        self.assertIn("char plugin_path[", alloc)
        self.assertNotIn("furi_string_alloc", alloc)

    def test_protocol_fal_load_precedes_poller_allocation(self) -> None:
        enter = function_body(
            self.source, "static void nfc_protocol_support_scene_read_on_enter("
        )
        plugin_load = enter.index("nfc_protocol_support_get(protocol, instance)")
        poller_alloc = enter.index("nfc_poller_alloc(instance->nfc, protocol)")
        self.assertLess(plugin_load, poller_alloc)
        self.assertIn("protocol_base->scene_read.on_enter(instance);", enter)

    def test_failed_protocol_load_returns_before_poller_allocation(self) -> None:
        enter = function_body(
            self.source, "static void nfc_protocol_support_scene_read_on_enter("
        )
        failure = enter.index("if(protocol_base == &nfc_protocol_support_empty)")
        release = enter.index("nfc_protocol_support_free(instance);")
        early_return = enter.index("return;", release)
        poller_alloc = enter.index("nfc_poller_alloc(instance->nfc, protocol)")
        self.assertLess(failure, release)
        self.assertLess(release, early_return)
        self.assertLess(early_return, poller_alloc)


if __name__ == "__main__":
    unittest.main()
