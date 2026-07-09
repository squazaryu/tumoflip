#!/usr/bin/env python3

import re
import unittest
from pathlib import Path

try:
    from .app_bridge_v2 import PAYLOAD_MAX
except ImportError:
    from app_bridge_v2 import PAYLOAD_MAX


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/wifi_mapper"


class WiFiMapperTest(unittest.TestCase):
    def test_app_is_external_module_one_fap(self) -> None:
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('appid="wifi_mapper"', manifest)
        self.assertIn("FlipperAppType.EXTERNAL", manifest)
        self.assertIn(
            'requires=["gui", "storage", "notification", "bt", "expansion_start"]',
            manifest,
        )
        self.assertIn('fap_category="Module One/ESP32 Wi-Fi"', manifest)
        self.assertIn('fap_version="0.9"', manifest)
        self.assertIn('fap_icon="wifi_mapper_10px.png"', manifest)
        self.assertNotIn("fap_icon_assets", manifest)
        self.assertTrue((APP_DIR / "wifi_mapper_10px.png").is_file())

    def test_uart_logger_uses_passive_scan_commands(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertIn("#define WIFI_MAPPER_SCAN_ALL_COMMAND \"scanall\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_SCAN_AP_COMMAND  \"scanap\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_WARDRIVE_COMMAND \"wardrive -serial\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_STOP_COMMAND \"stopscan\\r\\n\"", source)
        self.assertIn("WiFiMapperScanModeAll", source)
        self.assertIn("WiFiMapperScanModeWardrive", source)
        self.assertIn("WiFiMapperExportModeClean", source)
        self.assertIn("WiFiMapperExportModeRaw", source)
        self.assertIn("WiFiMapperScreenSession", source)
        self.assertIn("WiFiMapperSessionStats", source)
        self.assertIn("wifi_mapper_analyze_latest_session", source)
        self.assertIn("wifi_mapper_export_latest_session", source)
        self.assertIn("wifi_mapper_export_csv_to_geojson", source)
        self.assertIn("wifi_mapper_add_clean_export_row", source)
        self.assertIn("wifi_mapper_write_clean_geojson_feature", source)
        self.assertIn("wifi_mapper_write_geojson_feature", source)
        self.assertIn("wifi_mapper_write_escaped_json", source)
        self.assertIn("wifi_mapper_find_latest_session", source)
        self.assertIn("wifi_mapper_parse_marauder_ap_line", source)
        self.assertIn("wifi_mapper_parse_marauder_wardrive_line", source)
        self.assertIn("wifi_mapper_parse_structured_wifi_line", source)
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
        self.assertIn('furi_thread_alloc_ex("WiFiMapperRx", 2048', source)
        self.assertNotIn("char fields[WIFI_MAPPER_CSV_FIELDS]", source)
        self.assertIn("wifi_mapper_send_active_scan_command", source)
        self.assertIn('EXT_PATH("apps_data/wifi_mapper/sessions")', source)
        self.assertIn(
            '"tick_ms,type,rssi,channel,bssid,ssid,auth,lat,lon,alt,accuracy,raw\\n"',
            source,
        )
        self.assertIn('EXT_PATH("apps_data/wifi_mapper/exports")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 51, "OK", "Export")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 62, "R", "Clean/Raw")', source)
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

    def test_live_screen_uses_safe_text_legend_without_bottom_button_overlap(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertNotIn("wifi_mapper_icons.h", source)
        self.assertNotIn("canvas_draw_icon", source)
        self.assertIn("static int wifi_mapper_hint(", source)
        self.assertIn('wifi_mapper_hint(canvas, x, 51, "U", "Scan")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 51, "D", "Stop")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 51, "OK", "Rec")', source)
        self.assertIn('canvas_draw_str(canvas, x, 62, "Hold:")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 62, "D", "BLE")', source)
        self.assertIn('wifi_mapper_hint(canvas, x, 62, "OK", "Sess")', source)
        self.assertIn('if(model->logging) strlcat(chips, "REC ", sizeof(chips));', source)
        self.assertNotIn("wifi_mapper_draw_top_action", source)
        self.assertNotIn("canvas_draw_rbox(canvas, 94, 1, 34, 12, 2)", source)
        self.assertNotIn("elements_button_center(canvas", source)
        self.assertNotIn("elements_button_right(canvas", source)

    def test_live_relay_contract_is_stable_and_opt_in(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertIn("#include <bt/bt_service/bt.h>", source)
        self.assertIn('#define WIFI_MAPPER_RELAY_APP_ID      "wifi_mapper"', source)
        self.assertIn('#define WIFI_MAPPER_RELAY_COMMAND     "live_line"', source)
        self.assertIn("bt_app_bridge_send_text_v2(", source)
        self.assertIn("app->bt = furi_record_open(RECORD_BT);", source)
        self.assertIn("furi_record_close(RECORD_BT);", source)
        self.assertIn("WiFiMapperModel", source)
        self.assertIn("bool ble_relay;", source)
        self.assertIn('if(model->ble_relay) strlcat(chips, "BLE", sizeof(chips));', source)
        self.assertIn("(event->type == InputTypeLong) && (event->key == InputKeyDown)", source)
        self.assertIn('strlcpy(app->status, "BLE live on"', source)
        self.assertIn('strlcpy(app->status, "BLE live off"', source)
        self.assertIn("wifi_mapper_relay_flush_locked(app);", source)
        self.assertIn("wifi_mapper_close_log(app);", source)
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
            self.assertIn("150", doc)
            self.assertIn("best-effort", doc)
            self.assertIn("0/1", doc)

        self.assertIn("Hold `Down`", wifi_doc)
        self.assertIn("not persisted", wifi_doc)
        self.assertIn("App Bridge is disabled", wifi_doc)
        self.assertIn("WiFi Mapper live relay", bridge_doc)

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


if __name__ == "__main__":
    unittest.main()
