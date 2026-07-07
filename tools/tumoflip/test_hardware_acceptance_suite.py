#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumo_acceptance_suite"
APP_SOURCE = APP_DIR / "tumo_acceptance_suite.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class HardwareAcceptanceSuiteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_safe_module_one_external_fap(self) -> None:
        self.assertIn('appid="tumo_acceptance_suite"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('requires=["gui", "storage"]', self.manifest)
        self.assertIn('fap_category="Module One/Diagnostics"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())
        self.assertIn('icon="A_Plugins_14"', self.manifest)

    def test_report_covers_release_acceptance_inputs(self) -> None:
        for required in (
            "Tumoflip Hardware Acceptance Suite",
            "version_get_version(version)",
            "version_get_githash(version)",
            "version_get_dirty_flag(version)",
            "furi_hal_info_get_api_version(&api_major, &api_minor)",
            'EXT_PATH(".tumoflip/package-state.txt")',
            "storage_sd_status(app->storage)",
            "memmgr_get_free_heap()",
            "memmgr_get_minimum_free_heap()",
            "memmgr_heap_get_max_free_block()",
            "furi_hal_infrared_is_busy()",
            "furi_hal_power_check_otg_fault()",
            "Hardware-only transmit/receive tests: SKIP by design",
        ):
            self.assertIn(required, self.source)

    def test_app_checks_key_packaged_paths_without_active_transmit(self) -> None:
        for required in (
            'EXT_PATH("apps/Module One/Diagnostics/cockpit.fap")',
            'EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap")',
            'EXT_PATH("apps/Module One/Field/field_logger.fap")',
            'EXT_PATH("apps/Module One/Signals/signal_workbench.fap")',
            'EXT_PATH("apps/Module One/Sensors BME280/module_one_sensor_logger.fap")',
            'EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap")',
            'EXT_PATH("apps/Module One/Macros/tumo_macro_deck.fap")',
            'EXT_PATH("apps/Module One/IR Blaster/tumo_ir_lab.fap")',
            'EXT_PATH("apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap")',
            'EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")',
        ):
            self.assertIn(required, self.source)

        self.assertNotIn("furi_hal_infrared_async_tx_start", self.source)
        self.assertNotIn("loader_start", self.source)
        self.assertNotIn("furi_hal_power_enable_otg", self.source)

    def test_report_export_and_cockpit_package_route(self) -> None:
        self.assertIn('EXT_PATH("apps_data/tumo_acceptance_suite")', self.source)
        self.assertIn('"/acceptance_%s.txt"', self.source)
        self.assertIn("storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)", self.source)
        self.assertIn("storage_file_sync(file)", self.source)
        self.assertIn("System: Acceptance", self.cockpit)
        self.assertIn("System: Runtime Trace", self.cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Diagnostics/tumo_acceptance_suite.fap")',
            self.cockpit,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap")',
            self.cockpit,
        )
        self.assertIn(
            '"apps/Module One/Diagnostics/tumo_acceptance_suite.fap"',
            self.validator,
        )
        self.assertIn(
            '"apps/Module One/Diagnostics/runtime_trace_viewer.fap"',
            self.validator,
        )
        self.assertIn(
            '"apps/Module One/Field/field_logger.fap"',
            self.validator,
        )
        self.assertIn(
            '"apps/Module One/Signals/signal_workbench.fap"',
            self.validator,
        )


if __name__ == "__main__":
    unittest.main()
