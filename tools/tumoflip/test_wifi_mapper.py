#!/usr/bin/env python3

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

try:
    from .app_bridge_v2 import PAYLOAD_MAX
except ImportError:
    from app_bridge_v2 import PAYLOAD_MAX


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/wifi_mapper"
FIXTURE_DIR = REPO_ROOT / "tools/tumoflip/fixtures/wifi_mapper"


class WiFiMapperTest(unittest.TestCase):
    def test_inspector_core_host_contract(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("No host C compiler")

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "wifi_mapper_inspector_host_test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(APP_DIR),
                    str(APP_DIR / "wifi_mapper_inspector.c"),
                    str(REPO_ROOT / "tools/tumoflip/wifi_mapper_inspector_host_test.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            result = subprocess.run(
                [
                    str(executable),
                    str(FIXTURE_DIR / "baseline.csv"),
                    str(FIXTURE_DIR / "current.csv"),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("wifi_mapper_inspector_host_test: PASS", result.stdout)

    def test_insights_core_host_contract(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("No host C compiler")

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "wifi_mapper_insights_host_test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(APP_DIR),
                    str(APP_DIR / "wifi_mapper_insights.c"),
                    str(REPO_ROOT / "tools/tumoflip/wifi_mapper_insights_host_test.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            result = subprocess.run(
                [str(executable)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("wifi_mapper_insights_host_test: PASS", result.stdout)

    def test_app_is_external_module_one_fap(self) -> None:
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('appid="wifi_mapper"', manifest)
        self.assertIn('name="TumoSurvey"', manifest)
        self.assertIn("FlipperAppType.EXTERNAL", manifest)
        self.assertIn(
            'requires=["gui", "storage", "notification", "bt", "expansion_start"]',
            manifest,
        )
        self.assertIn('fap_category="Module One/ESP32 Wi-Fi"', manifest)
        self.assertIn('fap_version="1.2.0"', manifest)
        self.assertIn('fap_icon="wifi_mapper_10px.png"', manifest)
        self.assertIn('fap_icon_assets="icons"', manifest)
        self.assertTrue((APP_DIR / "wifi_mapper_10px.png").is_file())
        for icon in (
            "ButtonUp_7x4.png",
            "ButtonDown_7x4.png",
            "ButtonCenter_7x7.png",
            "ButtonRight_4x7.png",
            "Pin_back_arrow_10x8.png",
        ):
            self.assertTrue((APP_DIR / "icons" / icon).is_file(), icon)

    def test_uart_logger_uses_passive_scan_commands(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        for macro, command in (
            ("WIFI_MAPPER_SCAN_ALL_COMMAND", "scanall"),
            ("WIFI_MAPPER_SCAN_AP_COMMAND", "scanap"),
            ("WIFI_MAPPER_WARDRIVE_COMMAND", "wardrive -serial"),
            ("WIFI_MAPPER_STOP_COMMAND", "stopscan"),
        ):
            self.assertRegex(
                source,
                rf'#define\s+{macro}\s+"{re.escape(command)}\\r\\n"',
            )
        self.assertIn("WiFiMapperScanModeAll", source)
        self.assertIn("WiFiMapperScanModeWardrive", source)
        self.assertIn("WiFiMapperExportModeClean", source)
        self.assertIn("WiFiMapperExportModeRaw", source)
        self.assertIn("WiFiMapperScreenSession", source)
        self.assertIn("WiFiMapperSessionStats", source)
        self.assertIn("WiFiMapperInsights", source)
        self.assertIn("WiFiMapperScreenInsights", source)
        self.assertIn("WiFiMapperScreenAbout", source)
        self.assertIn("WiFiMapperSessionPageSecurity", source)
        self.assertIn("WiFiMapperSessionPageChannels", source)
        self.assertIn("WiFiMapperSessionPageBaseline", source)
        self.assertIn("wifi_mapper_read_session_stats", source)
        self.assertIn("delta_unique", source)
        self.assertIn('"No earlier baseline"', source)
        self.assertIn('"AP limit reached"', source)
        self.assertIn("wifi_mapper_analyze_latest_session", source)
        self.assertIn("wifi_mapper_export_latest_session", source)
        self.assertIn("wifi_mapper_export_csv_to_geojson", source)
        self.assertIn("wifi_mapper_add_clean_export_row", source)
        self.assertIn("wifi_mapper_write_clean_geojson_feature", source)
        self.assertIn("wifi_mapper_write_geojson_feature", source)
        self.assertIn("wifi_mapper_write_escaped_json", source)
        self.assertIn("wifi_mapper_find_latest_session", source)
        self.assertIn("wifi_mapper_find_adjacent_session", source)
        self.assertIn("WiFiMapperScreenInspector", source)
        self.assertIn("WiFiMapperScreenInspectorList", source)
        self.assertIn("WiFiMapperScreenLocator", source)
        self.assertIn("WIFI_MAPPER_BASELINE_PATH", source)
        self.assertIn("wifi_mapper_read_baseline_reference", source)
        self.assertIn("wifi_mapper_write_baseline_reference", source)
        self.assertIn('"No AP data to pin"', source)
        self.assertIn("wifi_mapper_read_inspector_snapshot", source)
        self.assertIn("wifi_mapper_inspector_compare", source)
        self.assertIn("wifi_mapper_locator_start", source)
        self.assertIn("wifi_mapper_locator_stop", source)
        self.assertIn("WIFI_MAPPER_SCAN_AP_COMMAND", source)
        self.assertIn("wifi_mapper_select_adjacent_session", source)
        self.assertIn("wifi_mapper_parse_marauder_ap_line", source)
        self.assertIn("wifi_mapper_parse_marauder_wardrive_line", source)
        self.assertIn("wifi_mapper_parse_structured_wifi_line", source)
        self.assertIn("if(!cursor || (strlen(cursor) < 17U))", source)
        self.assertIn("wifi_mapper_parse_csv_ap_row", source)
        self.assertIn("wifi_mapper_parse_export_row", source)
        self.assertIn("wifi_mapper_geo_number_valid", source)
        self.assertIn("wifi_mapper_toggle_export_mode", source)
        self.assertIn('"ESSID"', source)
        self.assertIn('"SSID"', source)
        self.assertIn('"Ch"', source)
        self.assertIn('"wardrive"', source)
        self.assertIn('"_%s.geojson"', source)
        self.assertIn('"raw"', source)
        self.assertIn('"clean"', source)
        self.assertIn("samples", source)
        self.assertIn("best_rssi", source)
        self.assertIn("FeatureCollection", source)
        self.assertIn("storage_dir_open", source)
        self.assertIn("storage_file_read", source)
        self.assertIn("WIFI_MAPPER_MAX_UNIQUE", source)
        self.assertIn("WIFI_MAPPER_MAX_EXPORT_FEATURES", source)
        self.assertIn("WiFiMapperSessionScratch* scratch = malloc", source)
        self.assertIn("WiFiMapperExportScratch* scratch = malloc", source)
        self.assertIn(
            'furi_thread_alloc_ex("WiFiMapperRx", 3 * 1024',
            source,
        )
        self.assertNotIn("const WiFiMapperInsights insights = app->insights", source)
        self.assertNotIn("const WiFiMapperSessionStats session = app->session", source)
        self.assertNotIn("char fields[WIFI_MAPPER_CSV_FIELDS]", source)
        self.assertIn("wifi_mapper_send_active_scan_command", source)
        self.assertIn('EXT_PATH("apps_data/wifi_mapper/sessions")', source)
        self.assertIn(
            '"tick_ms,type,rssi,channel,bssid,ssid,auth,lat,lon,alt,accuracy,raw\\n"',
            source,
        )
        self.assertIn('EXT_PATH("apps_data/wifi_mapper/exports")', source)
        self.assertIn(
            'model->session_page == WiFiMapperSessionPageBaseline ? "Inspect" : "Export"',
            source,
        )
        self.assertIn('elements_button_right(canvas, "Next")', source)
        self.assertIn("FuriHalSerialIdUsart", source)
        self.assertIn("#include <expansion/expansion.h>", source)
        self.assertIn("Expansion* expansion;", source)
        self.assertIn("bool expansion_disabled;", source)
        self.assertIn("app->expansion = furi_record_open(RECORD_EXPANSION);", source)
        self.assertIn("expansion_disable(app->expansion);", source)
        self.assertIn("app->serial_handle = furi_hal_serial_control_acquire", source)
        self.assertLess(
            source.index("expansion_disable(app->expansion);"),
            source.index("app->serial_handle = furi_hal_serial_control_acquire"),
        )
        self.assertIn("expansion_enable(app->expansion);", source)
        self.assertIn("furi_record_close(RECORD_EXPANSION);", source)
        self.assertIn("FSOM_CREATE_ALWAYS", source)
        self.assertIn('"wifi_%04u%02u%02u_%02u%02u%02u.csv"', source)
        self.assertIn('strlcat(app->log_temp_path, ".part"', source)
        self.assertIn("storage_file_sync(app->log_file);", source)
        self.assertIn(
            "storage_common_rename(app->storage, app->log_temp_path, app->log_path)",
            source,
        )

    def test_screens_use_standard_soft_buttons(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertIn('#include "wifi_mapper_icons.h"', source)
        self.assertIn('canvas_draw_str(canvas, 0, 10, "TumoSurvey")', source)
        self.assertIn('elements_button_left(canvas, "Mode")', source)
        self.assertIn(
            'elements_button_center(canvas, model->logging ? "Stop" : "Start")',
            source,
        )
        self.assertIn('elements_button_right(canvas, "Data")', source)
        self.assertIn('if(model->logging) strlcat(chips, "LIVE ", sizeof(chips));', source)
        self.assertIn("static void wifi_mapper_draw_insights(", source)
        self.assertIn("static void wifi_mapper_draw_about(", source)
        self.assertIn('"TumoSurvey v1.2.0"', source)
        self.assertIn('"Survey + AP Inspector"', source)
        self.assertIn('"squazaryu/tumoflip"', source)
        self.assertIn('elements_button_left(canvas, "Set Base")', source)
        self.assertIn('elements_button_center(canvas, "Changes")', source)
        self.assertIn('elements_button_right(canvas, "All")', source)
        self.assertIn('elements_button_center(canvas, "Locate")', source)
        self.assertIn(
            'elements_button_center(canvas, model->locator_running ? "Stop" : "Start")',
            source,
        )
        self.assertNotIn("wifi_mapper_draw_top_action", source)
        self.assertNotIn("canvas_draw_rbox(canvas, 94, 1, 34, 12, 2)", source)
        self.assertNotIn("static int wifi_mapper_hint(", source)

    def test_live_relay_contract_is_stable_and_opt_in(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertIn("#include <bt/bt_service/bt.h>", source)
        self.assertRegex(
            source,
            r'#define\s+WIFI_MAPPER_RELAY_APP_ID\s+"wifi_mapper"',
        )
        self.assertRegex(
            source,
            r'#define\s+WIFI_MAPPER_RELAY_COMMAND\s+"live_line"',
        )
        self.assertRegex(
            source,
            r'#define\s+WIFI_MAPPER_RELAY_START_COMMAND\s+"survey_start"',
        )
        self.assertRegex(
            source,
            r'#define\s+WIFI_MAPPER_RELAY_STOP_COMMAND\s+"survey_stop"',
        )
        self.assertIn("bt_app_bridge_send_text_v2(", source)
        self.assertIn("app->bt = furi_record_open(RECORD_BT);", source)
        self.assertIn("furi_record_close(RECORD_BT);", source)
        self.assertIn("WiFiMapperModel", source)
        self.assertIn("bool ble_relay;", source)
        self.assertIn('if(model->ble_relay) strlcat(chips, "BLE", sizeof(chips));', source)
        self.assertIn("(event->type == InputTypeLong) && (event->key == InputKeyDown)", source)
        self.assertIn("app->ble_relay = true;", source)
        self.assertIn("app->ble_relay = false;", source)
        self.assertIn(
            "wifi_mapper_relay_state_locked(app, WIFI_MAPPER_RELAY_START_COMMAND);",
            source,
        )
        self.assertIn(
            "wifi_mapper_relay_state_locked(app, WIFI_MAPPER_RELAY_STOP_COMMAND);",
            source,
        )
        self.assertLess(
            source.index("app->ble_relay = true;"),
            source.index(
                "wifi_mapper_send_active_scan_command(app);",
                source.index("app->ble_relay = true;"),
            ),
        )
        self.assertIn('strlcpy(app->status, "BLE live on"', source)
        self.assertIn('strlcpy(app->status, "BLE live off"', source)
        self.assertIn('if(!app->logging) {', source)
        self.assertIn('strlcpy(app->status, "Start first"', source)
        self.assertIn("wifi_mapper_relay_flush_locked(app);", source)
        self.assertIn("wifi_mapper_close_log(app);", source)
        self.assertIn("const bool active = app->logging || (app->log_file != NULL);", source)
        self.assertIn("char relay_buffer[WIFI_MAPPER_RELAY_PAYLOAD_MAX + 1U];", source)
        self.assertNotIn("Issue #6", source)

        match = re.search(r"#define WIFI_MAPPER_RELAY_PAYLOAD_MAX\s+(\d+)U", source)
        self.assertIsNotNone(match)
        self.assertLessEqual(int(match.group(1)), PAYLOAD_MAX)

    def test_live_relay_contract_is_documented_for_companion(self) -> None:
        wifi_doc = (REPO_ROOT / "docs/wifi-mapper.md").read_text(encoding="utf-8")
        bridge_doc = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")

        for doc in (wifi_doc, bridge_doc):
            self.assertIn("wifi_mapper", doc)
            self.assertIn("live_line", doc)
            self.assertIn("survey_start", doc)
            self.assertIn("survey_stop", doc)
            self.assertIn("150", doc)
            self.assertIn("best-effort", doc)
            self.assertIn("0/1", doc)

        self.assertIn("Starting a survey automatically arms", wifi_doc)
        self.assertIn("not persisted", wifi_doc)
        self.assertIn("App Bridge is disabled", wifi_doc)
        self.assertIn("TumoSurvey live relay", bridge_doc)

    def test_uart_logger_does_not_expose_attack_commands(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(
            encoding="utf-8"
        ).casefold()
        forbidden_terms = (
            "deauth",
            "beaconspam",
            "sniffpmkid",
            "sniffpwn",
            "attack",
            "evilportal",
        )

        for term in forbidden_terms:
            self.assertNotIn(term, source)

    def test_inspector_contract_is_documented(self) -> None:
        document = (REPO_ROOT / "docs/wifi-mapper.md").read_text(encoding="utf-8")

        self.assertIn("## Survey Inspector", document)
        self.assertIn("/ext/apps_data/wifi_mapper/baseline.txt", document)
        self.assertIn("`New`", document)
        self.assertIn("`Gone`", document)
        self.assertIn("`Changed`", document)
        self.assertIn("RSSI Locator", document)
        self.assertIn("32-AP Inspector limit", document)


if __name__ == "__main__":
    unittest.main()
