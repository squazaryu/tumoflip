#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class Iso14443_3aCascadeTests(unittest.TestCase):
    def test_sak_is_the_authority_for_uid_completeness(self) -> None:
        header = (
            REPO_ROOT
            / "lib/nfc/protocols/iso14443_3a/iso14443_3a_poller_i.h"
        ).read_text(encoding="utf-8")
        source = (
            REPO_ROOT
            / "lib/nfc/protocols/iso14443_3a/iso14443_3a_poller_i.c"
        ).read_text(encoding="utf-8")
        body = function_body(source, "iso14443_3a_poller_activate")

        self.assertIn("ISO14443_3A_POLLER_SAK_CASCADE_BIT      (0x04U)", header)
        self.assertNotIn("ISO14443_3A_POLLER_SDD_CL", header)
        self.assertIn(
            "instance->col_res.sel_resp.sak & ISO14443_3A_POLLER_SAK_CASCADE_BIT",
            body,
        )
        self.assertNotIn(
            "instance->col_res.sel_req.nfcid[0] == ISO14443_3A_POLLER_SDD_CL",
            body,
        )

    def test_fourth_cascade_is_rejected_before_uid_copy(self) -> None:
        source = (
            REPO_ROOT
            / "lib/nfc/protocols/iso14443_3a/iso14443_3a_poller_i.c"
        ).read_text(encoding="utf-8")
        body = function_body(source, "iso14443_3a_poller_activate")

        guard = body.index("instance->col_res.cascade_level >= 2")
        partial_uid_copy = body.index(
            "&instance->col_res.sel_req.nfcid[1]", guard
        )
        self.assertLess(guard, partial_uid_copy)
        self.assertIn("instance->state = Iso14443_3aPollerStateColResFailed", body)
        self.assertIn("ret = Iso14443_3aErrorColResFailed", body)


if __name__ == "__main__":
    unittest.main()
