#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/module_one_cockpit"
APP_SOURCE = APP_DIR / "module_one_cockpit.c"
APP_MANIFEST = APP_DIR / "application.fam"

COCKPIT_FAP_ROUTES = {
    "tumo_uart_console": (
        "apps_data/module_one_cockpit/modules/tumo_uart_console.fap"
    ),
    "tumo_ir_lab": "apps/Module One/IR Blaster/tumo_ir_lab.fap",
    "tumoflip_xremote": "apps/Module One/IR Blaster/tumoflip_xremote.fap",
    "wifi_mapper": "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
    "ble_gatt_lab": "apps/Module One/BLE/ble_gatt_lab.fap",
    "app_bridge_terminal": "apps/Module One/BLE/app_bridge_terminal.fap",
    "tumoflow": "apps/Module One/Automation/tumoflow.fap",
    "tumo_macro_deck": "apps/Module One/Macros/tumo_macro_deck.fap",
    "tumoscript": "apps/Module One/Scripts/tumoscript.fap",
    "field_logger": "apps/Module One/Field/field_logger.fap",
    "signal_workbench": "apps/Module One/Signals/signal_workbench.fap",
    "tumoscope": "apps/Module One/Signals/tumoscope.fap",
    "tumonet_gateway": "apps/Module One/Network/tumonet_gateway.fap",
    "tumovgm_bridge": "apps/Module One/VGM/tumovgm_bridge.fap",
    "tumomodule_runtime": "apps/Module One/Modules/tumomodule_runtime.fap",
    "tumokey": "apps/Module One/Security/tumokey.fap",
    "module_one_sensor_logger": (
        "apps/Module One/Sensors BME280/module_one_sensor_logger.fap"
    ),
    "tumocard_os": "apps/Module One/NFC/tumocard_os.fap",
    "tumotag_verify": "apps/Module One/NFC/tumotag_verify.fap",
    "arf_subghz_full": "apps/ARF Tools/arf_subghz_full.fap",
    "arf_status": "apps_data/arf_subghz_full/modules/arf_status.fap",
    "tumo_acceptance_suite": (
        "apps/Module One/Diagnostics/tumo_acceptance_suite.fap"
    ),
    "runtime_trace_viewer": (
        "apps/Module One/Diagnostics/runtime_trace_viewer.fap"
    ),
}


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
        self.assertIn("UART: Field Console", self.source)
        self.assertIn("BLE: Terminal", self.source)
        self.assertIn("Automation: TumoFlow", self.source)
        self.assertIn("Field: Logger", self.source)
        self.assertIn("Signals: TumoSpectrum", self.source)
        self.assertIn("Signals: Profiles", self.source)
        self.assertIn("Signals: TumoScope", self.source)
        self.assertIn("Network: TumoNet", self.source)
        self.assertIn("VGM: Bridge", self.source)
        self.assertIn("Modules: Runtime", self.source)
        self.assertIn("Security: TumoKey", self.source)
        self.assertIn("NFC: TumoCard OS", self.source)
        self.assertIn("NFC: Tag Verify", self.source)
        self.assertIn("Macros: TumoScript", self.source)
        self.assertIn("System: Acceptance", self.source)
        self.assertIn("System: Runtime Trace", self.source)
        self.assertIn(
            'EXT_PATH("apps_data/module_one_cockpit/modules/tumo_uart_console.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/BLE/app_bridge_terminal.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Automation/tumoflow.fap")',
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
        self.assertIn(
            'EXT_PATH("apps/Module One/Field/field_logger.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Signals/signal_workbench.fap")',
            self.source,
        )
        self.assertIn('"tumospectrum_profiles"', self.source)
        self.assertNotIn("protocol_compiler.fap", self.source)
        self.assertIn(
            'EXT_PATH("apps/Module One/Signals/tumoscope.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Network/tumonet_gateway.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/VGM/tumovgm_bridge.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Modules/tumomodule_runtime.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Security/tumokey.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/Scripts/tumoscript.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/NFC/tumocard_os.fap")',
            self.source,
        )
        self.assertIn(
            'EXT_PATH("apps/Module One/NFC/tumotag_verify.fap")',
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
        self.assertIn("TumoScope for GPIO capture", self.source)

    def test_nested_launches_restore_cockpit_selection(self) -> None:
        self.assertIn("loader_clear_launch_queue(app->loader);", self.source)
        self.assertIn("loader_enqueue_launch(app->loader, target->target", self.source)
        self.assertIn("loader_get_application_launch_path(app->loader, self_path)", self.source)
        self.assertIn("snprintf(selected_item_arg", self.source)
        self.assertLess(
            self.source.index("loader_clear_launch_queue(app->loader)"),
            self.source.index("loader_enqueue_launch(app->loader, target->target"),
        )

    def test_all_storage_backed_routes_have_packaged_faps(self) -> None:
        manifests = "\n".join(
            path.read_text(encoding="utf-8")
            for root in (REPO_ROOT / "applications", REPO_ROOT / "applications_user")
            for path in root.rglob("application.fam")
        )
        validator = (
            REPO_ROOT / "tools/tumoflip/validate_release.py"
        ).read_text(encoding="utf-8")

        routed_paths = {
            route
            for route in COCKPIT_FAP_ROUTES.values()
            if f'EXT_PATH("{route}")' in self.source
        }
        self.assertEqual(routed_paths, set(COCKPIT_FAP_ROUTES.values()))

        for appid, route in COCKPIT_FAP_ROUTES.items():
            with self.subTest(appid=appid, route=route):
                self.assertIn(f'appid="{appid}"', manifests)
                if appid == "arf_status":
                    self.assertIn('"arf_status"', validator)
                    self.assertIn("ARF_MODULE_ROOT", validator)
                elif appid == "arf_subghz_full":
                    self.assertIn('"arf_subghz_full"', validator)
                    self.assertIn("ARF_VISIBLE_PATHS", validator)
                else:
                    self.assertIn(f'"{route}"', validator)


if __name__ == "__main__":
    unittest.main()
