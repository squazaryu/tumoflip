#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/module_one_sensor_logger"
APP_SOURCE = APP_DIR / "module_one_sensor_logger.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class ModuleOneSensorLoggerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="module_one_sensor_logger"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('fap_category="Module One"', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertIn('requires=["gui", "storage", "notification"]', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_logger_exports_csv_jsonl_and_gpx_sessions(self) -> None:
        self.assertIn("MODULE_ONE_SENSOR_SESSIONS_DIR", self.source)
        self.assertIn("%s.csv", self.source)
        self.assertIn("%s.jsonl", self.source)
        self.assertIn("%s.gpx", self.source)
        self.assertIn("timestamp,uptime_ms,sample,source", self.source)
        self.assertIn("Tumoflip Sensor Logger", self.source)

    def test_gps_and_bme280_paths_are_independent(self) -> None:
        self.assertIn("FuriHalSerialIdUsart", self.source)
        self.assertIn("module_one_sensor_parse_nmea_line", self.source)
        self.assertIn('module_one_sensor_nmea_type_is(fields[0], "RMC")', self.source)
        self.assertIn('module_one_sensor_nmea_type_is(fields[0], "GGA")', self.source)
        self.assertIn("MODULE_ONE_SENSOR_BME280_ADDR_A", self.source)
        self.assertIn("MODULE_ONE_SENSOR_BME280_ADDR_B", self.source)
        self.assertIn("module_one_sensor_bme280_sample", self.source)
        self.assertIn('return "gps+bme280";', self.source)
        self.assertIn('return "gps";', self.source)
        self.assertIn('return "bme280";', self.source)

    def test_stop_path_syncs_and_closes_outputs(self) -> None:
        self.assertIn("module_one_sensor_stop_logging", self.source)
        self.assertIn("module_one_sensor_sync_files", self.source)
        self.assertIn("storage_file_sync(app->csv_file)", self.source)
        self.assertIn("storage_file_sync(app->jsonl_file)", self.source)
        self.assertIn("storage_file_sync(app->gpx_file)", self.source)
        self.assertIn("</trkseg></trk>", self.source)
        self.assertNotIn("furi_string_reset((FuriString*)NULL)", self.source)

    def test_run_screen_uses_non_blocking_stop_hint(self) -> None:
        self.assertIn("module_one_sensor_draw_action_hint", self.source)
        self.assertIn('module_one_sensor_draw_action_hint(canvas, "OK Stop")', self.source)
        self.assertNotIn("module_one_sensor_draw_top_action", self.source)
        self.assertNotIn("canvas_draw_rbox(canvas, 96, 1, 32, 12, 2)", self.source)

    def test_cockpit_and_package_route_sensor_logger(self) -> None:
        self.assertIn("Sensors: Logger", self.cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/module_one_sensor_logger.fap")',
            self.cockpit,
        )
        self.assertIn(
            '"apps/Module One/module_one_sensor_logger.fap"',
            self.validator,
        )


if __name__ == "__main__":
    unittest.main()
