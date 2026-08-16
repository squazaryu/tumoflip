#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/field_logger"
APP_SOURCE = APP_DIR / "field_logger.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class FieldLoggerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_is_module_one_external_fap(self) -> None:
        self.assertIn('appid="field_logger"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('fap_category="Module One/Field"', self.manifest)
        self.assertIn('fap_dist_path="apps/Module One/Field/field_logger.fap"', self.manifest)
        self.assertIn('requires=["gui", "storage", "notification", "bt"]', self.manifest)
        self.assertIn('fap_libs=["tumoflip_device_services"]', self.manifest)
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_field_timeline_exports_expected_formats(self) -> None:
        for required in (
            "FIELD_LOGGER_SESSIONS_DIR",
            "%s.csv",
            "%s.jsonl",
            "%s.gpx",
            "schema,timestamp,uptime_ms,record,kind,source",
            "Tumoflip Field Logger",
        ):
            self.assertIn(required, self.source)

    def test_rf_path_is_receive_only_notebook_import(self) -> None:
        self.assertIn("FIELD_LOGGER_RF_NOTEBOOK_CSV", self.source)
        self.assertIn('EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")', self.source)
        self.assertIn("field_logger_read_latest_rf_observation", self.source)
        self.assertIn('"rf_observation"', self.source)
        self.assertIn('"rf_missing"', self.source)
        self.assertNotIn("subghz_txrx", self.source)
        self.assertNotIn("furi_hal_subghz", self.source)

    def test_missing_sources_degrade_gracefully(self) -> None:
        self.assertIn("FIELD_LOGGER_SENSOR_SESSIONS_DIR", self.source)
        self.assertIn("No ARF Frequency Analyzer notebook observation", self.source)
        self.assertIn('\\"gps_fix\\":%s', self.source)
        self.assertIn('\\"gps_accuracy_m\\":null', self.source)
        self.assertIn('"Phone GPS unavailable"', self.source)
        self.assertIn(
            '"gps_timestamp,temperature_c,pressure_hpa,humidity_percent,rf_frequency_hz,"',
            self.source,
        )
        self.assertIn('\\"temperature_c\\":null', self.source)
        self.assertIn('\\"rf\\":null', self.source)

    def test_cockpit_acceptance_and_package_routes_field_logger(self) -> None:
        for text in (self.cockpit, self.acceptance, self.validator):
            self.assertIn("field_logger.fap", text)
        self.assertIn("Field: Logger", self.cockpit)
        self.assertIn("Field Logger", self.acceptance)
        self.assertIn('"apps/Module One/Field/field_logger.fap"', self.validator)


if __name__ == "__main__":
    unittest.main()
