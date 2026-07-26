#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
LISTENER_HEADER = REPO_ROOT / "lib/nfc/protocols/iso15693_3/iso15693_3_listener_i.h"
LISTENER_SOURCE = REPO_ROOT / "lib/nfc/protocols/iso15693_3/iso15693_3_listener.c"
PARSER_SOURCE = REPO_ROOT / "lib/signal_reader/parsers/iso15693/iso15693_parser.c"


def extract_function(source: str, name: str) -> str:
    start = source.index(f"static Iso15693ParserCommand {name}(")
    next_function = source.find(
        "static Iso15693ParserCommand ",
        start + 1,
    )
    return source[start : next_function if next_function != -1 else len(source)]


class Iso15693LargeMultiBlockTest(unittest.TestCase):
    def setUp(self) -> None:
        self.listener_header = LISTENER_HEADER.read_text(encoding="utf-8")
        self.listener_source = LISTENER_SOURCE.read_text(encoding="utf-8")
        self.parser_source = PARSER_SOURCE.read_text(encoding="utf-8")

    def test_listener_buffer_covers_256_four_byte_blocks(self) -> None:
        match = re.search(
            r"#define ISO15693_3_LISTENER_BUFFER_SIZE "
            r"\(1U \+ 256U \* 4U \+ 2U\)",
            self.listener_header,
        )
        self.assertIsNotNone(match)
        self.assertEqual(1 + 256 * 4 + 2, 1027)

    def test_listener_buffer_is_scoped_to_listener_lifetime(self) -> None:
        alloc = self.listener_source.index(
            "bit_buffer_alloc(ISO15693_3_LISTENER_BUFFER_SIZE)"
        )
        free = self.listener_source.index("bit_buffer_free(instance->tx_buffer)")
        self.assertLess(alloc, free)

    def test_parser_checks_capacity_before_every_append_path(self) -> None:
        for function_name in (
            "iso15693_parser_parse_1_out_of_4",
            "iso15693_parser_parse_1_out_of_256",
        ):
            with self.subTest(function=function_name):
                function = extract_function(self.parser_source, function_name)
                normalized = re.sub(r"\s+", " ", function)
                capacity_guard = normalized.index(
                    "bit_buffer_get_capacity_bytes(instance->parsed_frame)"
                )
                append = normalized.index("bit_buffer_append_byte(")
                self.assertLess(capacity_guard, append)
                self.assertIn(
                    "return Iso15693ParserCommandFail;",
                    normalized[capacity_guard:append],
                )


if __name__ == "__main__":
    unittest.main()
