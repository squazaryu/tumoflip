#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/app_bridge_terminal"
APP_SOURCE = APP_DIR / "app_bridge_terminal.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
BRIDGE_DOC = REPO_ROOT / "docs/app-bridge-v2.md"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class AppBridgeTerminalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.bridge_doc = BRIDGE_DOC.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_safe_module_one_external_fap(self) -> None:
        self.assertIn('appid="app_bridge_terminal"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('requires=["gui", "storage", "bt"]', self.manifest)
        self.assertIn('fap_category="Module One/BLE"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_terminal_contract_is_whitelisted_and_session_scoped(self) -> None:
        for required in (
            '#define APP_BRIDGE_TERMINAL_APP_ID "app_bridge_terminal"',
            "bt_app_bridge_get_pubsub(app->bt)",
            "furi_pubsub_subscribe",
            "furi_pubsub_unsubscribe",
            "bt_app_bridge_send_text_v2",
            'strcmp(event->command, "hello") == 0',
            'strcmp(event->command, "status") == 0',
            'strcmp(event->command, "help") == 0',
            'strcmp(event->command, "ping") == 0',
            'strcmp(event->command, "echo") == 0',
            'strcmp(event->command, "emit") == 0',
            'strcmp(event->command, "release") == 0',
            "app_bridge_terminal_session_required",
            "app_bridge_terminal_payload_has_sid",
            "APP_BRIDGE_TERMINAL_SESSION_TIMEOUT_MS 30000U",
            "BtAppBridgeFlagResponse",
            "BtAppBridgeFlagError",
        ):
            self.assertIn(required, self.source)

        for forbidden in (
            "system(",
            "popen(",
            "loader_enqueue_launch",
            "bt_disconnect",
            "bt_profile_start",
            "bt_profile_restore_default",
            "furi_hal_bt_start_scan",
            "furi_hal_bt_start_tone_tx",
        ):
            self.assertNotIn(forbidden, self.source)

    def test_logging_and_event_stream_are_bounded(self) -> None:
        self.assertIn('EXT_PATH("apps_data/app_bridge_terminal")', self.source)
        self.assertIn(
            '"timestamp,event,protocol,request_id,flags,app_id,command,payload,status,error\\n"',
            self.source,
        )
        self.assertIn("storage_file_sync(app->log_file)", self.source)
        self.assertIn("APP_BRIDGE_TERMINAL_PAYLOAD_SIZE 160U", self.source)
        self.assertIn('"schema=1;type=%.12s;seq=%lu;owner=%.16s;text=%.48s"', self.source)
        self.assertIn('"Unsupported chunked request"', self.source)

    def test_cockpit_acceptance_package_and_docs_route_terminal(self) -> None:
        self.assertIn("BLE: Terminal", self.cockpit)
        self.assertIn('EXT_PATH("apps/Module One/BLE/app_bridge_terminal.fap")', self.cockpit)
        self.assertIn('EXT_PATH("apps/Module One/BLE/app_bridge_terminal.fap")', self.acceptance)
        self.assertIn('"apps/Module One/BLE/app_bridge_terminal.fap"', self.validator)
        self.assertIn("App Bridge Terminal", self.bridge_doc)
        self.assertIn("App ID | `app_bridge_terminal`", self.bridge_doc)
        self.assertIn("Commands | `hello`, `ping`, `status`, `help`, `echo`, `emit`, `release`", self.bridge_doc)


if __name__ == "__main__":
    unittest.main()
