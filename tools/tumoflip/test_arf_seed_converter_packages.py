#!/usr/bin/env python3
"""Contracts for ARF-derived ProtoPirate converter and Seed BF packages."""

import unittest
from pathlib import Path

from tools.tumoflip.validate_release import (
    PACKAGE_ONLY_PACKAGE_FILES,
    PACKAGE_ONLY_PACKAGE_GROUPS,
    PACKAGE_RELEASE_OVERLAY_FILES,
    PACKAGE_RELEASE_OVERLAY_GROUPS,
)


ROOT = Path(__file__).resolve().parents[2]
ARF_ROOT = "apps_data/arf_subghz_full/modules"
CONVERTER_TARGET = f"{ARF_ROOT}/protopirate_to_subghz.fap"
SEED_TARGET = f"{ARF_ROOT}/renault_seed_bf.fap"


class ArfSeedConverterPackageTest(unittest.TestCase):
    def test_both_tools_are_package_only_arf_modules(self) -> None:
        for appid, target in (
            ("protopirate_to_subghz", CONVERTER_TARGET),
            ("renault_seed_bf", SEED_TARGET),
        ):
            manifest = (ROOT / f"applications/system/{appid}/application.fam").read_text(
                encoding="utf-8"
            )
            self.assertIn("fap_package_only=True", manifest)
            self.assertIn('fap_dist_path="apps_data/arf_subghz_full/modules/{filename}"', manifest)
            self.assertIn(target, PACKAGE_ONLY_PACKAGE_FILES)
            self.assertEqual(PACKAGE_ONLY_PACKAGE_GROUPS[target], "arf")
            self.assertIn(target, PACKAGE_RELEASE_OVERLAY_FILES)
            self.assertEqual(PACKAGE_RELEASE_OVERLAY_GROUPS[target], "arf")

    def test_converter_is_recursive_and_cancelable(self) -> None:
        source = (
            ROOT / "applications/system/protopirate_to_subghz/protopirate_to_subghz.c"
        ).read_text(encoding="utf-8")
        self.assertIn("P2S_WORKER_FLAG_STOP", source)
        self.assertIn("dir_walk_set_recursive", source)
        self.assertIn("P2sEventFinished", source)

    def test_seed_bf_is_manual_and_cooperative(self) -> None:
        source = (ROOT / "applications/system/renault_seed_bf/renault_seed_bf.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("hitag2_seed_recover_ex", source)
        self.assertIn("view_dispatcher_send_custom_event", source)
        self.assertIn("furi_delay_ms(1)", source)
        self.assertIn('"Recovered"', source)
        self.assertIn('"Seed"', source)
        self.assertNotIn("SubGhzWorker", source)


if __name__ == "__main__":
    unittest.main()
