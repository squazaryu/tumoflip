#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumo_macro_deck"
APP_SOURCE = APP_DIR / "tumo_macro_deck.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
BRIDGE_DOC = REPO_ROOT / "docs/app-bridge-v2.md"
MACRO_DOC = REPO_ROOT / "docs/tumo-macro-deck.md"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"
SAMPLE = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/tumo_macro_deck/macros/safe_demo.tmacro"
)


class TumoMacroDeckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.bridge_doc = BRIDGE_DOC.read_text(encoding="utf-8")
        cls.macro_doc = MACRO_DOC.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")
        cls.sample = SAMPLE.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="tumo_macro_deck"', self.manifest)
        self.assertIn("apptype=FlipperAppType.EXTERNAL", self.manifest)
        self.assertIn('requires=["gui", "storage", "bt"]', self.manifest)
        self.assertIn('fap_category="Module One/Macros"', self.manifest)

    def test_sd_backed_macro_runner_contract(self) -> None:
        self.assertIn('EXT_PATH("apps_data/tumo_macro_deck")', self.source)
        self.assertIn('"/macros"', self.source)
        self.assertIn('"/runs"', self.source)
        self.assertIn("storage_dir_read", self.source)
        self.assertIn("safe_demo.tmacro", self.source)
        self.assertIn("policy stop", self.sample)
        self.assertIn("delay 250", self.sample)
        self.assertIn("ble_event safe_demo_started", self.sample)
        self.assertIn("wait_button Press OK", self.sample)

    def test_execution_is_cancelable_and_logged(self) -> None:
        self.assertIn("tumo_macro_deck_wait_delay", self.source)
        self.assertIn("tumo_macro_deck_wait_ok_or_back", self.source)
        self.assertIn("InputKeyBack", self.source)
        self.assertIn('"timestamp,macro,step,line,command,status,detail\\n"', self.source)
        self.assertIn("storage_file_sync(app->log_file)", self.source)
        self.assertIn("continue_on_error", self.source)

    def test_app_bridge_event_contract(self) -> None:
        self.assertIn('#define TUMO_MACRO_DECK_APP_ID "tumo_macro_deck"', self.source)
        self.assertIn("bt_app_bridge_send_text_v2", self.source)
        self.assertIn("bt_app_bridge_send_text(app->bt", self.source)
        self.assertIn("App ID | `tumo_macro_deck`", self.bridge_doc)
        self.assertIn("Command | `event`", self.bridge_doc)

    def test_hardware_steps_are_gated_for_first_increment(self) -> None:
        self.assertIn("confirm IR", self.source)
        self.assertIn("confirm GPIO", self.source)
        self.assertIn("hardware step gated", self.source)
        self.assertNotIn("furi_hal_infrared_async_tx_start", self.source)
        self.assertNotIn("furi_hal_gpio_write", self.source)

    def test_cockpit_package_and_docs_route_macro_deck(self) -> None:
        self.assertIn("Macros: Deck", self.cockpit)
        self.assertIn('EXT_PATH("apps/Module One/Macros/tumo_macro_deck.fap")', self.cockpit)
        self.assertIn('"apps/Module One/Macros/tumo_macro_deck.fap"', self.validator)
        self.assertIn('"apps_data/tumo_macro_deck/macros/safe_demo.tmacro"', self.validator)
        self.assertIn("Tumo Macro Deck", self.macro_doc)


if __name__ == "__main__":
    unittest.main()
