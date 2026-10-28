"""New field tools must be independent package-only exports, built by both producers."""
from pathlib import Path
import unittest
from tools.tumoflip.validate_release import (
    package_extapp_exports, PACKAGE_ONLY_PACKAGE_FILES, PACKAGE_RELEASE_OVERLAY_GROUPS,
)

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {
    'servo_tester_plus': 'apps/GPIO/servo_tester_plus.fap',
    'power_journal': 'apps/Tools/Diagnostics/power_journal.fap',
    'tumo_uhf_inventory': 'apps/GPIO/tumo_uhf_inventory.fap',
    'sensor_watch': 'apps/Sub-GHz/sensor_watch.fap',
    'tumo_varta': 'apps/RFID/tumo_varta.fap',
}


class FieldBatchPackageTests(unittest.TestCase):
    def test_existing_field_hosts_bump_their_feature_versions(self):
        self.assertIn('fap_version="1.3"', (ROOT / 'applications/system/device_library/application.fam').read_text())
        self.assertIn('fap_version="3.6.0"', (ROOT / 'applications_user/signal_workbench/application.fam').read_text())

    def test_all_field_batch_exports_are_source_owned_base_packages(self):
        exports = package_extapp_exports()
        for app, path in TARGETS.items():
            self.assertEqual(exports.get(app + '.fap'), path, app)
            self.assertIn(path, PACKAGE_ONLY_PACKAGE_FILES)
            self.assertEqual(PACKAGE_RELEASE_OVERLAY_GROUPS.get(path), 'base')

    def test_both_firmware_producers_build_field_batch_and_run_regressions(self):
        for name in ('pr-build.yml', 'release.yml'):
            workflow = (ROOT / '.github/workflows' / name).read_text()
            for app in TARGETS:
                self.assertIn('fap_' + app, workflow, name)
            for test in ('test_servo_plus', 'test_power_history', 'test_uhf_inventory',
                         'test_sensor_cards', 'test_varta_inspect', 'test_field_report'):
                self.assertIn(test, workflow, name)


if __name__ == '__main__': unittest.main()
