#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class BleKillerIntegrationTest(unittest.TestCase):
    def test_ble_killer_is_visible_arf_tool(self) -> None:
        manifest = (REPO_ROOT / "applications_user/ble_killer/application.fam").read_text(
            encoding="utf-8"
        )

        self.assertIn('appid="ble_killer"', manifest)
        self.assertIn('name="[BLE] BLE Killer"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn("D4C1 Labs; tumoflip integration", manifest)
        self.assertNotIn("fap_dist_path", manifest)

    def test_ble_killer_is_source_only_import(self) -> None:
        app_dir = REPO_ROOT / "applications_user/ble_killer"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])

    def test_ble_killer_is_documented_as_uart_opt_in(self) -> None:
        readme = (REPO_ROOT / "applications_user/ble_killer/README.md").read_text(
            encoding="utf-8"
        )
        source = (REPO_ROOT / "applications_user/ble_killer/ble_killer.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("UART controller", readme)
        self.assertIn("Flipper's built-in BLE stack", readme)
        self.assertIn("BLE App Bridge", readme)
        self.assertIn("furi_hal_serial_tx", source)
        self.assertNotIn("RECORD_BT", source)

    def test_release_and_deploy_include_ble_killer_as_visible_app(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('"ble_killer"', validate)
        self.assertIn("ble_killer.fap", deploy)
        self.assertIn("BLE Killer", docs)


if __name__ == "__main__":
    unittest.main()
