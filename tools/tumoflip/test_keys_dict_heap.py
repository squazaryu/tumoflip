#!/usr/bin/env python3
"""Regression contracts for heap-stable dictionary iteration."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
KEYS_DICT = REPO_ROOT / "lib/toolbox/keys_dict.c"
CLASSIC_SCENES = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic_extra_scenes.c"
)


def region(contents: str, start: str, end: str) -> str:
    start_index = contents.index(start)
    end_index = contents.index(end, start_index)
    return contents[start_index:end_index]


class KeysDictHeapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.keys_dict = KEYS_DICT.read_text(encoding="utf-8")
        cls.classic_scenes = CLASSIC_SCENES.read_text(encoding="utf-8")

    def test_dictionary_owns_one_reusable_scratch_buffer(self) -> None:
        struct = region(self.keys_dict, "struct KeysDict {", "};")
        self.assertIn("FuriString* scratch;", struct)

        alloc = region(self.keys_dict, "KeysDict* keys_dict_alloc(", "void keys_dict_free(")
        self.assertIn("instance->scratch = furi_string_alloc();", alloc)
        self.assertIn(
            "keys_dict_read_key_line(instance, instance->scratch, &is_endfile)",
            alloc,
        )

        free = region(self.keys_dict, "void keys_dict_free(", "static void keys_dict_int_to_str(")
        self.assertIn("furi_string_free(instance->scratch);", free)

    def test_next_key_does_not_allocate_per_dictionary_entry(self) -> None:
        next_key = region(
            self.keys_dict,
            "bool keys_dict_get_next_key(",
            "static bool keys_dict_is_key_present_str(",
        )
        self.assertIn("keys_dict_get_next_key_str(instance, instance->scratch)", next_key)
        self.assertNotIn("furi_string_alloc", next_key)
        self.assertNotIn("furi_string_free", next_key)

    def test_cuid_dictionary_uses_shared_opaque_api(self) -> None:
        self.assertNotIn("struct KeysDict {", self.classic_scenes)
        self.assertIn("keys_dict_alloc(", self.classic_scenes)
        self.assertIn("const size_t cuid_key_size", self.classic_scenes)
        self.assertNotIn("dict->key_size", self.classic_scenes)
        self.assertNotIn("dict->total_keys", self.classic_scenes)


if __name__ == "__main__":
    unittest.main()
