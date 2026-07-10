#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = (
    REPO_ROOT / "applications_user/protopirate/helpers/protopirate_storage.c"
)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"static bool {name}\([^{{]+\) \{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")

    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]

    raise AssertionError(f"unterminated function: {name}")


class ProtoPirateStorageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_key2_prefers_eight_byte_hex_with_legacy_fallback(self) -> None:
        body = function_body(self.source, "protopirate_storage_copy_key2")

        eight_byte_read = (
            'protopirate_storage_copy_hex_fixed(save_file, flipper_format, "Key2", 8, '
            "&copied)"
        )
        legacy_fallback = (
            'protopirate_storage_copy_hex_or_u32(save_file, flipper_format, "Key2", 4)'
        )

        self.assertIn(eight_byte_read, body)
        self.assertIn(legacy_fallback, body)
        self.assertLess(body.index(eight_byte_read), body.index(legacy_fallback))
        self.assertIn("if(copied)", body)

    def test_capture_writer_uses_key2_compatibility_helper(self) -> None:
        body = function_body(self.source, "protopirate_storage_write_capture_data")

        self.assertIn("protopirate_storage_copy_key2(save_file, flipper_format)", body)
        self.assertNotIn(
            'protopirate_storage_copy_hex_or_u32(save_file, flipper_format, "Key2", 4)',
            body,
        )


if __name__ == "__main__":
    unittest.main()
