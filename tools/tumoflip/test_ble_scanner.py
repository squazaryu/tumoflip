#!/usr/bin/env python3
"""Static contracts for the bounded, passive BLE scanner FAP."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/ble_scanner"
SOURCE = APP_DIR / "ble_scanner.c"
MANIFEST = APP_DIR / "application.fam"
COCKPIT = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class BleScannerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.manifest = MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_is_separate_passive_module_one_fap(self) -> None:
        self.assertIn('appid="ble_scanner"', self.manifest)
        self.assertIn("apptype=FlipperAppType.EXTERNAL", self.manifest)
        self.assertIn('requires=["gui", "bt"]', self.manifest)
        self.assertIn('fap_category="Module One/BLE"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())
        self.assertIn('"BLE: Scanner"', self.cockpit)
        self.assertIn('apps/Module One/BLE/ble_scanner.fap', self.cockpit)
        self.assertIn('apps/Module One/BLE/ble_scanner.fap', self.validator)

    def test_requires_consent_and_never_exposes_active_operations(self) -> None:
        self.assertIn("Use only with permission", self.source)
        self.assertIn("bool authorized;", self.source)
        self.assertIn("furi_hal_bt_scan_start", self.source)
        self.assertIn("furi_hal_bt_scan_stop", self.source)
        self.assertIn("furi_delay_ms(50U)", self.source)
        self.assertNotIn("furi_hal_bt_gatt_", self.source)
        self.assertNotIn("aci_", self.source)

    def test_scan_is_bounded_and_address_deduplicated(self) -> None:
        self.assertIn("BLE_SCANNER_MAX_RESULTS 16U", self.source)
        self.assertIn("BLE_SCANNER_SCAN_MS     8000U", self.source)
        self.assertIn("address_type", self.source)
        self.assertIn("memcmp(app->entries[index].result.address", self.source)
        self.assertIn("furi_message_queue_put(app->queue, &event, 0U)", self.source)
        self.assertIn("if(app->entry_count >= BLE_SCANNER_MAX_RESULTS) return;", self.source)


if __name__ == "__main__":
    unittest.main()
