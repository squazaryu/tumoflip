#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/wifi_mapper"


class WiFiMapperTest(unittest.TestCase):
    def test_app_is_external_module_one_fap(self) -> None:
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('appid="wifi_mapper"', manifest)
        self.assertIn("FlipperAppType.EXTERNAL", manifest)
        self.assertIn('fap_category="Module One/ESP32 Wi-Fi"', manifest)
        self.assertIn('fap_icon="wifi_mapper_10px.png"', manifest)
        self.assertTrue((APP_DIR / "wifi_mapper_10px.png").is_file())

    def test_uart_logger_uses_passive_scan_commands(self) -> None:
        source = (APP_DIR / "wifi_mapper.c").read_text(encoding="utf-8")

        self.assertIn("#define WIFI_MAPPER_SCAN_ALL_COMMAND \"scanall\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_SCAN_AP_COMMAND  \"scanap\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_WARDRIVE_COMMAND \"wardrive -serial\\r\\n\"", source)
        self.assertIn("#define WIFI_MAPPER_STOP_COMMAND \"stopscan\\r\\n\"", source)
        self.assertIn("WiFiMapperScanModeAll", source)
        self.assertIn("WiFiMapperScanModeWardrive", source)
        self.assertIn("WiFiMapperScreenSession", source)
        self.assertIn("WiFiMapperSessionStats", source)
        self.assertIn("wifi_mapper_analyze_latest_session", source)
        self.assertIn("wifi_mapper_find_latest_session", source)
        self.assertIn("wifi_mapper_parse_marauder_ap_line", source)
        self.assertIn("wifi_mapper_parse_marauder_wardrive_line", source)
        self.assertIn("wifi_mapper_parse_csv_ap_row", source)
        self.assertIn('"ESSID:"', source)
        self.assertIn('"Ch:"', source)
        self.assertIn('"wardrive"', source)
        self.assertIn("storage_dir_open", source)
        self.assertIn("storage_file_read", source)
        self.assertIn("WIFI_MAPPER_MAX_UNIQUE", source)
        self.assertIn("WiFiMapperSessionScratch* scratch = malloc", source)
        self.assertIn('furi_thread_alloc_ex("WiFiMapperRx", 2048', source)
        self.assertNotIn("char fields[WIFI_MAPPER_CSV_FIELDS]", source)
        self.assertIn("wifi_mapper_send_active_scan_command", source)
        self.assertIn('EXT_PATH("apps_data/wifi_mapper/sessions")', source)
        self.assertIn(
            '"tick_ms,type,rssi,channel,bssid,ssid,auth,lat,lon,alt,accuracy,raw\\n"',
            source,
        )
        self.assertIn("FuriHalSerialIdUsart", source)
        self.assertIn("FSOM_CREATE_ALWAYS", source)

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
