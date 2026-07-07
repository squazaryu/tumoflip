#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/module_one_cockpit"
APP_SOURCE = APP_DIR / "module_one_cockpit.c"
APP_MANIFEST = APP_DIR / "application.fam"


class ModuleOneCockpitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="module_one_cockpit"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('fap_category="Module One/Diagnostics"', self.manifest)
        self.assertIn('fap_dist_path="apps/Module One/Diagnostics/cockpit.fap"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())
        self.assertIn("BLE: GATT Lab", self.source)
        self.assertIn("System: Acceptance", self.source)
        self.assertIn("System: Runtime Trace", self.source)
        self.assertIn(
            'EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Diagnostics/tumo_acceptance_suite.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap")',
            self.source,
        )

    def test_cockpit_pro_exports_diagnostic_report(self) -> None:
        self.assertIn("MODULE_ONE_COCKPIT_DATA_DIR", self.source)
        self.assertIn('EXT_PATH("apps_data/module_one_cockpit")', self.source)
        self.assertIn("MODULE_ONE_COCKPIT_PACKAGE_STATE_PATH", self.source)
        self.assertIn('EXT_PATH(".tumoflip/package-state.txt")', self.source)
        self.assertIn("ModuleOneCockpitActionExportDiagnostics", self.source)
        self.assertIn('"Diagnostics: Export"', self.source)
        self.assertIn("module_one_cockpit_export_report", self.source)
        self.assertIn('"/diagnostics_%s.txt"', self.source)
        self.assertIn("storage_common_mkdir(storage, path)", self.source)
        self.assertIn("storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)", self.source)
        self.assertIn("storage_file_write(file, text, size)", self.source)

    def test_health_report_uses_passive_checks_only(self) -> None:
        self.assertIn("Module One Cockpit Pro", self.source)
        self.assertIn("module_one_cockpit_append_firmware_report", self.source)
        self.assertIn("module_one_cockpit_append_storage_report(app, output)", self.source)
        self.assertIn("module_one_cockpit_append_memory_report", self.source)
        self.assertIn("version_get_version(version)", self.source)
        self.assertIn("version_get_githash(version)", self.source)
        self.assertIn("version_get_dirty_flag(version)", self.source)
        self.assertIn("furi_hal_info_get_api_version(&api_major, &api_minor)", self.source)
        self.assertIn("storage_sd_status(app->storage)", self.source)
        self.assertIn("storage_file_exists(app->storage, MODULE_ONE_COCKPIT_PACKAGE_STATE_PATH)", self.source)
        self.assertIn("memmgr_heap_get_max_free_block()", self.source)
        self.assertIn("Runtime health", self.source)
        self.assertIn("furi_hal_infrared_is_busy()", self.source)
        self.assertIn("furi_hal_serial_control_is_busy(FuriHalSerialIdUsart)", self.source)
        self.assertIn("furi_hal_serial_control_is_busy(FuriHalSerialIdLpuart)", self.source)
        self.assertIn("furi_hal_power_sleep_available()", self.source)
        self.assertNotIn("furi_hal_power_insomnia_level", self.source)
        self.assertIn("furi_hal_power_check_otg_fault()", self.source)
        self.assertIn("module_one_cockpit_probe_bme280", self.source)
        self.assertIn("module_one_cockpit_i2c_ready(0x76)", self.source)
        self.assertIn("module_one_cockpit_i2c_ready(0x77)", self.source)
        self.assertIn("furi_hal_i2c_acquire(&furi_hal_i2c_handle_external)", self.source)
        self.assertIn("furi_hal_i2c_release(&furi_hal_i2c_handle_external)", self.source)
        self.assertNotIn("furi_hal_power_enable_otg", self.source)
        self.assertNotIn("furi_hal_power_disable_otg", self.source)
        self.assertIn("Acceptance: export release smoke reports after each flash", self.source)

    def test_nested_launches_restore_cockpit_selection(self) -> None:
        self.assertIn("loader_clear_launch_queue(app->loader);", self.source)
        self.assertIn("loader_enqueue_launch(app->loader, target->target", self.source)
        self.assertIn("loader_get_application_launch_path(app->loader, self_path)", self.source)
        self.assertIn("snprintf(selected_item_arg", self.source)
        self.assertLess(
            self.source.index("loader_clear_launch_queue(app->loader)"),
            self.source.index("loader_enqueue_launch(app->loader, target->target"),
        )


if __name__ == "__main__":
    unittest.main()
