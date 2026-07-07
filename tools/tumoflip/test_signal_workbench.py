#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/signal_workbench"
APP_SOURCE = APP_DIR / "signal_workbench.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class SignalWorkbenchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="signal_workbench"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('fap_category="Module One/Signals"', self.manifest)
        self.assertIn('fap_dist_path="apps/Module One/Signals/signal_workbench.fap"', self.manifest)
        self.assertIn('requires=["gui", "storage", "notification"]', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_ir_analysis_is_bounded_and_format_aware(self) -> None:
        for required in (
            "SIGNAL_WORKBENCH_IR_BUFFER_SIZE 8192U",
            "SIGNAL_WORKBENCH_IR_TIMING_LIMIT 256U",
            'SIGNAL_WORKBENCH_IR_DIR EXT_PATH("infrared")',
            '"type:"',
            '"frequency:"',
            '"duty_cycle:"',
            '"data:"',
            '"RAW_Data:"',
            "signal_workbench_parse_timings_from_key",
        ):
            self.assertIn(required, self.source)

    def test_rf_metadata_is_receive_only(self) -> None:
        self.assertIn("SIGNAL_WORKBENCH_RF_NOTEBOOK_CSV", self.source)
        self.assertIn('EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")', self.source)
        self.assertIn("signal_workbench_read_latest_rf_observation", self.source)
        self.assertIn("Receive-only RF Context", self.source)
        self.assertNotIn("subghz_txrx", self.source)
        self.assertNotIn("furi_hal_subghz", self.source)

    def test_gpio_path_is_explicit_bounded_and_restored(self) -> None:
        for required in (
            "SIGNAL_WORKBENCH_GPIO_CYCLES 8U",
            "SIGNAL_WORKBENCH_GPIO_HIGH_US 500U",
            "SIGNAL_WORKBENCH_GPIO_LOW_US 500U",
            "GPIO: Run Pulse PC0",
            "gpio_ext_pc0",
            "GpioModeOutputPushPull",
            "GpioModeAnalog",
        ):
            self.assertIn(required, self.source)

    def test_cockpit_acceptance_and_package_routes_signal_workbench(self) -> None:
        for text in (self.cockpit, self.acceptance, self.validator):
            self.assertIn("signal_workbench.fap", text)
        self.assertIn("Signals: Workbench", self.cockpit)
        self.assertIn("Signal Workbench", self.acceptance)
        self.assertIn('"apps/Module One/Signals/signal_workbench.fap"', self.validator)


if __name__ == "__main__":
    unittest.main()
