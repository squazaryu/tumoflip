#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/ble_gatt_lab"
APP_SOURCE = APP_DIR / "ble_gatt_lab.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
BRIDGE_DOC = REPO_ROOT / "docs/app-bridge-v2.md"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class BleGattLabTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.bridge_doc = BRIDGE_DOC.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_safe_module_one_external_fap(self) -> None:
        self.assertIn('appid="ble_gatt_lab"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('requires=["gui", "storage", "bt"]', self.manifest)
        self.assertIn('fap_category="Module One/BLE"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_app_bridge_request_response_contract(self) -> None:
        self.assertIn('#define BLE_GATT_LAB_APP_ID "ble_gatt_lab"', self.source)
        self.assertIn("bt_app_bridge_get_pubsub(app->bt)", self.source)
        self.assertIn("furi_pubsub_subscribe", self.source)
        self.assertIn("furi_pubsub_unsubscribe", self.source)
        self.assertIn("bt_app_bridge_send_text_v2", self.source)
        self.assertIn("bt_app_bridge_send_text(app->bt", self.source)
        self.assertIn('strcmp(event->command, "ping") == 0', self.source)
        self.assertIn('ble_gatt_lab_send_text(app, event, "pong", "ok", false)', self.source)
        self.assertIn('strcmp(event->command, "status") == 0', self.source)
        self.assertIn('strcmp(event->command, "echo") == 0', self.source)
        self.assertIn('"unsupported_command"', self.source)
        self.assertIn("BtAppBridgeFlagResponse", self.source)
        self.assertIn("BtAppBridgeFlagError", self.source)
        self.assertIn("chunking_unsupported", self.source)

    def test_log_and_reconnect_safety(self) -> None:
        self.assertIn('EXT_PATH("apps_data/ble_gatt_lab")', self.source)
        self.assertIn('"timestamp,event,protocol,request_id,flags,app_id,command,payload,error\\n"', self.source)
        self.assertIn("storage_file_sync(app->log_file)", self.source)
        self.assertIn("furi_message_queue_put(app->queue, &event, 0)", self.source)
        self.assertNotIn("bt_profile_start", self.source)
        self.assertNotIn("bt_profile_restore_default", self.source)
        self.assertNotIn("bt_disconnect", self.source)
        self.assertNotIn("furi_hal_bt_start_scan", self.source)
        self.assertNotIn("furi_hal_bt_start_tone_tx", self.source)

    def test_cockpit_package_and_docs_route_lab(self) -> None:
        self.assertIn("BLE: GATT Lab", self.cockpit)
        self.assertIn('EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap")', self.cockpit)
        self.assertIn('"apps/Module One/BLE/ble_gatt_lab.fap"', self.validator)
        self.assertIn("BLE GATT Lab diagnostics", self.bridge_doc)
        self.assertIn("App ID | `ble_gatt_lab`", self.bridge_doc)
        self.assertIn("Commands | `ping`, `status`, `echo`", self.bridge_doc)


if __name__ == "__main__":
    unittest.main()
