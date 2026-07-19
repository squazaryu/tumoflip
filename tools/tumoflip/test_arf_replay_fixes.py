#!/usr/bin/env python3
"""Source-level regression checks for selected ARF replay fixes."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfReplayFixesTest(unittest.TestCase):
    def test_keeloq_persists_recovered_learning_type(self) -> None:
        source = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/scenes/subghz_scene_keeloq_decrypt.c"
        ).read_text(encoding="utf-8")

        self.assertIn("ctx->recovered_type", source)
        self.assertIn("ctx->recovered_type :", source)
        self.assertIn("subghz_keeloq_keys_add(", source)
        self.assertIn("learning_type,", source)
        self.assertIn("entry->type = learning_type;", source)

    def test_kia_v5_reencodes_replay_and_accepts_extended_capture(self) -> None:
        source = (REPO_ROOT / "lib/subghz/protocols/kia_v5.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("static uint64_t kia_v5_encode_data", source)
        self.assertIn("instance->generic.data = kia_v5_encode_data(", source)
        self.assertIn("instance->replay_data = instance->generic.data;", source)
        self.assertIn(
            "SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);",
            source,
        )
        self.assertIn("SubGhzProtocolStatusErrorParserBitCount", source)


if __name__ == "__main__":
    unittest.main()
