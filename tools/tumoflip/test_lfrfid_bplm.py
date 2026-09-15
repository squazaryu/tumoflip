#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class LfrfidBplmTest(unittest.TestCase):
    def test_hitag_writers_share_reader_to_tag_timings(self) -> None:
        header = (REPO_ROOT / "lib/lfrfid/tools/hitag_bplm.h").read_text(
            encoding="utf-8"
        )
        micro = (REPO_ROOT / "lib/lfrfid/tools/hitagmicro.c").read_text(
            encoding="utf-8"
        )
        hitags = (REPO_ROOT / "lib/lfrfid/tools/hitags.c").read_text(
            encoding="utf-8"
        )

        for value in ("64u", "96u", "160u"):
            self.assertIn(value, header)
        for source in (micro, hitags):
            self.assertIn('#include "hitag_bplm.h"', source)
            self.assertIn("LFRFID_HITAG_BPLM_GAP_US", source)
            self.assertIn("LFRFID_HITAG_BPLM_BIT0_ON_US", source)
            self.assertIn("LFRFID_HITAG_BPLM_BIT1_ON_US", source)

        # Protocol-specific constants must not silently fork the shared cell
        # timing values again in either implementation.
        for source in (micro, hitags):
            self.assertNotRegex(source, r"#define HITAGS?(MICRO)?_(?:GAP|BIT[01]_ON)_US")


if __name__ == "__main__":
    unittest.main()
