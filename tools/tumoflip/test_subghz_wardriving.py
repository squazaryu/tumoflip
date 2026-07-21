#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/subghz_wardriving"


class SubGhzWardrivingTest(unittest.TestCase):
    def read(self, relative_path: str) -> str:
        return (APP_ROOT / relative_path).read_text(encoding="utf-8")

    def test_api88_fap_embeds_uart_gps_plugin(self) -> None:
        manifest = self.read("application.fam")
        api_symbols = (REPO_ROOT / "targets/f7/api_symbols.csv").read_text(
            encoding="utf-8"
        )

        self.assertIn("Version,+,88.0,,", api_symbols)
        self.assertIn('appid="subghz_wardriving"', manifest)
        self.assertIn('targets=["f7"]', manifest)
        self.assertIn('fap_category="Sub-GHz"', manifest)
        self.assertIn('requires=["subghz_radio_broker"]', manifest)
        self.assertIn('appid="subghz_plugin_gps"', manifest)
        self.assertIn("fal_embedded=True", manifest)

    def test_companion_only_gps_rpc_is_not_imported(self) -> None:
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in APP_ROOT.rglob("*")
            if path.suffix in {".c", ".h"}
        )
        for symbol in (
            "gps_request_stream",
            "gps_set_location_callback",
            "gps_stop_stream",
        ):
            self.assertNotIn(symbol, source)

        gps_header = self.read("helpers/subghz_wardriving_gps.h")
        self.assertIn("SubGhzGpsProtocolOff = 0", gps_header)
        self.assertIn("SubGhzGpsProtocolNmea = 2", gps_header)
        self.assertIn("SubGhzGpsProtocolUbox = 3", gps_header)

    def test_busy_uart_degrades_without_furi_check(self) -> None:
        gps_source = self.read("helpers/subghz_wardriving_gps.c")
        plugin_source = self.read("helpers/subghz_wardriving_gps_plugin.c")

        self.assertIn("if(!subghz_gps->serial_handle)", gps_source)
        self.assertNotIn("furi_check(subghz_gps->serial_handle)", gps_source)
        self.assertIn("if(!subghz_gps->serial_handle)", plugin_source)
        self.assertIn("return NULL;", plugin_source)

    def test_gps_fix_validation_and_timeout_use_absolute_time(self) -> None:
        gps_source = self.read("helpers/subghz_wardriving_gps.c")
        receiver_source = self.read("scenes/subghz_wardriving_scene_receiver.c")

        self.assertIn("minmea_parse_rmc(&frame, line) && frame.valid", gps_source)
        self.assertIn("frame.fix_quality > 0", gps_source)
        self.assertIn("frame.status == MINMEA_GLL_STATUS_DATA_VALID", gps_source)
        self.assertIn("&& pvt.valid", gps_source)
        self.assertIn("fix_timestamp = furi_hal_rtc_get_timestamp()", gps_source)
        self.assertIn("(now - subghz->gps->fix_timestamp) > 15", receiver_source)
        self.assertNotIn("datetime.second - subghz->gps->fix_second", receiver_source)

    def test_nmea_buffer_reserves_terminator_and_recovers_from_long_lines(self) -> None:
        gps_header = self.read("helpers/subghz_wardriving_gps.h")
        gps_source = self.read("helpers/subghz_wardriving_gps.c")

        self.assertIn("uint8_t rx_buf[RX_BUF_SIZE + 1]", gps_header)
        self.assertIn("rx_offset == RX_BUF_SIZE", gps_source)
        self.assertIn("Dropping oversized NMEA sentence", gps_source)

    def test_state_is_zero_initialized_before_settings_are_applied(self) -> None:
        app_source = self.read("subghz_wardriving.c")
        settings_source = self.read("subghz_wardriving_last_settings.c")
        history_source = self.read("subghz_wardriving_history.c")

        self.assertIn("calloc(1, sizeof(SubGhz))", app_source)
        self.assertIn("calloc(1, sizeof(SubGhzLastSettings))", settings_source)
        self.assertIn("calloc(1, sizeof(SubGhzHistory))", history_source)
        tx_power_init = app_source.index(
            "subghz->tx_power = subghz->last_settings->tx_power;"
        )
        preset_apply = app_source.index("subghz_wardriving_txrx_set_preset_internal(")
        self.assertLess(tx_power_init, preset_apply)

    def test_gps_off_saves_standard_sub_files_without_nan_coordinates(self) -> None:
        history_source = self.read("subghz_wardriving_history.c")

        coordinate_guard = "if(!isnanf(latitude) && !isnanf(longitude))"
        self.assertIn(coordinate_guard, history_source)
        guard_index = history_source.index(coordinate_guard)
        self.assertGreater(history_source.index('"Lat: %f\\n"'), guard_index)
        self.assertGreater(history_source.index('"Lon: %f\\n"'), guard_index)

    def test_field_workflow_exposes_read_saved_and_radio_settings(self) -> None:
        start_scene = self.read("scenes/subghz_wardriving_scene_start.c")
        for label in ('"Read"', '"Saved"', '"Radio Settings"'):
            self.assertIn(label, start_scene)

    def test_baudrate_is_locked_only_when_uart_gps_is_off(self) -> None:
        settings_scene = self.read(
            "scenes/subghz_wardriving_scene_radio_settings.c"
        )

        self.assertIn('"GPS source"', settings_scene)
        self.assertIn("variable_item_set_locked(", settings_scene)
        self.assertIn(
            "gps_protocol_value[index] == SubGhzGpsProtocolOff", settings_scene
        )
        self.assertNotIn("index < SubGhzGpsProtocolNmea", settings_scene)

    def test_radio_lifecycle_is_owned_by_the_broker(self) -> None:
        txrx_header = self.read("helpers/subghz_wardriving_txrx_i.h")
        txrx_source = self.read("helpers/subghz_wardriving_txrx.c")

        self.assertIn("SubGhzRadioBroker* radio_broker;", txrx_header)
        self.assertIn("SubGhzRadioBrokerLease radio_lease;", txrx_header)
        for required in (
            "furi_record_open(RECORD_SUBGHZ_RADIO_BROKER)",
            "subghz_radio_broker_acquire(",
            '"subghz_wardrive"',
            "SubGhzRadioBrokerStateAsyncRx",
            "SubGhzRadioBrokerStateAsyncTx",
            "SubGhzRadioBrokerStateCleaningUp",
            "subghz_radio_broker_set_selected_device(",
            "subghz_radio_broker_release(",
            "furi_record_close(RECORD_SUBGHZ_RADIO_BROKER)",
        ):
            self.assertIn(required, txrx_source)


if __name__ == "__main__":
    unittest.main()
