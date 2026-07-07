#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumoscript"
APP_SOURCE = APP_DIR / "tumoscript.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"
SAMPLE = REPO_ROOT / "tools/tumoflip/sd_resources/apps_data/tumoscript/scripts/safe_demo.tscr"


class TumoScriptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")
        cls.sample = SAMPLE.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="tumoscript"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('requires=["gui", "storage", "bt", "notification"]', self.manifest)
        self.assertIn('fap_category="Module One/Scripts"', self.manifest)
        self.assertIn('fap_dist_path="apps/Module One/Scripts/tumoscript.fap"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_script_engine_has_validation_dry_run_and_bounds(self) -> None:
        for required in (
            "TUMOSCRIPT_MAX_STEPS 32U",
            "TUMOSCRIPT_MAX_EXECUTED_STEPS 64U",
            "TUMOSCRIPT_MAX_FILE_BYTES 8192U",
            "TUMOSCRIPT_GPIO_MAX_TOTAL_US 1000000U",
            "TUMOSCRIPT_IR_MAX_DURATION_MS 500U",
            "TumoScriptRunModeValidate",
            "TumoScriptRunModeDryRun",
            "tumoscript_validate_plan",
            "tumoscript_set_first_error",
        ):
            self.assertIn(required, self.source)

    def test_supported_actions_are_explicit_and_bounded(self) -> None:
        for required in (
            '"delay"',
            '"prompt"',
            '"bridge"',
            '"gpio_pulse"',
            '"ir_burst"',
            '"branch_ok"',
            '"branch_error"',
            '"goto"',
            "bt_app_bridge_send_text_v2",
            "furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeOutputPushPull)",
            "furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog)",
            "furi_hal_infrared_async_tx_start",
        ):
            self.assertIn(required, self.source)

        for forbidden in ("system(", "popen(", "loader_enqueue_launch", "furi_hal_subghz"):
            self.assertNotIn(forbidden, self.source)

    def test_packaged_sample_and_routes_are_declared(self) -> None:
        self.assertIn("safe_demo.tscr", self.sample)
        self.assertIn("gpio_pulse pc0 4 500 500", self.sample)
        self.assertIn("apps_data/tumoscript/scripts/safe_demo.tscr", self.validator)
        self.assertIn("apps/Module One/Scripts/tumoscript.fap", self.validator)
        self.assertIn("Macros: TumoScript", self.cockpit)
        self.assertIn('EXT_PATH("apps/Module One/Scripts/tumoscript.fap")', self.cockpit)
        self.assertIn("TumoScript", self.acceptance)


if __name__ == "__main__":
    unittest.main()
