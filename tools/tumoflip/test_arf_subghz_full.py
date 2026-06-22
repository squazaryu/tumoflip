#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfSubGhzFullTest(unittest.TestCase):
    def test_full_is_a_lightweight_hub(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="arf_subghz_full"', manifest)
        self.assertIn('sources=["arf_subghz_hub.c"]', manifest)
        self.assertNotIn('"scenes/*.c"', manifest)
        self.assertNotIn('"views/*.c"', manifest)

    def test_child_launchers_have_packaged_apps(self) -> None:
        start_scene = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")
        child_faps = set(re.findall(r'ARF_TOOLS_PATH "([^"/]+\.fap)', start_scene))
        app_manifests = "\n".join(
            path.read_text(encoding="utf-8")
            for root in (REPO_ROOT / "applications", REPO_ROOT / "applications_user")
            for path in root.rglob("application.fam")
        )

        self.assertEqual(
            child_faps,
            {
                "arf_car_emulate.fap",
                "arf_counter_bf.fap",
                "arf_frequency_analyzer.fap",
                "arf_keeloq.fap",
                "arf_psa_decrypt.fap",
                "arf_status.fap",
                "proto_pirate.fap",
                "rolljam.fap",
                "subghz_bruteforcer.fap",
            },
        )
        self.assertIn('.target = "Sub-GHz"', start_scene)
        for filename in child_faps:
            appid = filename.removesuffix(".fap")
            self.assertIn(f'appid="{appid}"', app_manifests)

    def test_legacy_duplicate_is_removed(self) -> None:
        self.assertFalse((REPO_ROOT / "applications_user/arf_subghz").exists())


if __name__ == "__main__":
    unittest.main()
