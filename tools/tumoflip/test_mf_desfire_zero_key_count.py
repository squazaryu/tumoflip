"""Regression contracts for DESFire cards and dumps with zero key counts."""

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DESFIRE_LOAD = REPO_ROOT / "lib/nfc/protocols/mf_desfire/mf_desfire.c"
DESFIRE_POLLER = REPO_ROOT / "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0

    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"unterminated function: {signature}")


class MfDesfireZeroKeyCountTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.load_source = DESFIRE_LOAD.read_text(encoding="utf-8")
        cls.poller_source = DESFIRE_POLLER.read_text(encoding="utf-8")

    def test_saved_dump_with_zero_master_keys_keeps_the_array_empty(self) -> None:
        load = function_body(self.load_source, "bool mf_desfire_load(")
        guarded_init = re.compile(
            r"if\(master_key_version_count > 0\)\s*\{\s*"
            r"simple_array_init\(data->master_key_versions, master_key_version_count\);",
            flags=re.DOTALL,
        )

        self.assertRegex(load, guarded_init)
        self.assertIn("PICC reports zero master keys, key versions skipped", load)

    def test_zero_key_application_is_not_an_assertion_or_allocation(self) -> None:
        poller = function_body(
            self.poller_source,
            "MfDesfireError mf_desfire_poller_read_key_versions(",
        )
        guarded_init = re.compile(
            r"if\(count > 0\)\s*\{\s*simple_array_init\(data, count\);",
            flags=re.DOTALL,
        )

        self.assertNotIn("furi_check(count > 0)", poller)
        self.assertRegex(poller, guarded_init)
        self.assertIn("Application reports zero keys, key versions skipped", poller)


if __name__ == "__main__":
    unittest.main()
