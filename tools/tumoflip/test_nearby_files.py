#!/usr/bin/env python3
"""Regression contracts for the Tumoflip Nearby Files package app."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nearby_files"


class NearbyFilesTest(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (APP / relative).read_text(encoding="utf-8")

    def test_is_a_package_only_api88_f7_app(self) -> None:
        manifest = self.read("application.fam")
        api = (ROOT / "targets/f7/api_symbols.csv").read_text(encoding="utf-8")

        self.assertIn('appid="nearby_files"', manifest)
        self.assertIn('targets=["f7"]', manifest)
        self.assertIn('fap_dist_path="apps/GPIO/nearby_files.fap"', manifest)
        self.assertIn('fap_libs=["tumoflip_device_services"]', manifest)
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn("Version,+,88.6,,", api)

    def test_companion_source_uses_one_shot_device_services(self) -> None:
        header = self.read("gps_reader.h")
        source = self.read("gps_reader_rpc.c")

        self.assertIn("tumoflip_device_services/tumoflip_device_services.h", header)
        self.assertIn("tumoflip_device_services_client_request_location", source)
        self.assertIn('"nearby_files"', source)
        self.assertIn("One-shot TumoCompanion location", header)
        for forbidden in (
            "#include <gps/gps.h>",
            "gps_request_stream(",
            "gps_set_location_callback(",
            "gps_stop_stream(",
            "furi_record_open(RECORD_GPS)",
        ):
            self.assertNotIn(forbidden, header + source)

    def test_upstream_gps_rpc_symbols_are_not_imported(self) -> None:
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in APP.rglob("*")
            if path.suffix in {".c", ".h"}
        )
        for symbol in (
            "gps_request_stream",
            "gps_set_location_callback",
            "gps_stop_stream",
        ):
            self.assertNotIn(symbol, source)

    def test_scan_and_launch_surface_is_preserved(self) -> None:
        source = self.read("nearby_files.c")
        for expected in (
            '".sub", ".nfc", ".rfid", ".ibtn"',
            '"/ext/subghz", "/ext/nfc", "/ext/lfrfid", "/ext/ibutton"',
            "nearby_files_parse_coordinates",
            "nearby_files_calculate_distance",
            "nearby_files_sort_by_distance",
            "nearby_files_add_gps_to_file",
            "loader_enqueue_launch",
        ):
            self.assertIn(expected, source)

    def test_phone_bridge_is_the_default_but_uart_remains_available(self) -> None:
        source = self.read("nearby_files.c")
        scenes = self.read("scenes/nearby_files_scene_gps_source.c")

        self.assertIn("GpsProtocol config_protocol = GpsProtocolRpc", source)
        self.assertIn('"TumoCompanion (BLE)"', scenes)
        self.assertIn('"UART module (NMEA)"', scenes)

    def test_original_license_is_carried(self) -> None:
        license_text = self.read("LICENSE")
        self.assertIn("MIT LICENSE", license_text)
        self.assertIn("Copyright (c) 2025 @Stichoza", license_text)


if __name__ == "__main__":
    unittest.main()
