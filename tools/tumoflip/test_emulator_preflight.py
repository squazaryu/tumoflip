"""The experimental emulator gate never installs tools or creates a disk image."""
from pathlib import Path
import importlib
import subprocess
import tempfile
import unittest


class EmulatorPreflightTests(unittest.TestCase):
    def test_missing_model_license_wrong_source_and_mac_runtime_are_not_acceptance(self):
        module = importlib.import_module('tools.tumoflip.emulator_preflight')
        with tempfile.TemporaryDirectory(prefix='emulator-gate-') as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            result = module.inspect(root, host='Darwin')
            self.assertFalse(result['bootTested'])
            self.assertFalse(result['hardwareAccepted'])
            self.assertFalse(result['readyToRun'])
            self.assertIn('sourcePinMismatch', result['blockers'])
            self.assertIn('modelLicenseMissing', result['blockers'])
            self.assertIn('linuxRuntimeRequired', result['blockers'])
            self.assertFalse((root / 'sdcard').exists())
            self.assertEqual(result['proposedDiskBudgetMiB'], 64)

    def test_no_checkout_is_a_reported_blocker_not_an_install_attempt(self):
        module = importlib.import_module('tools.tumoflip.emulator_preflight')
        result = module.inspect(Path('/nonexistent/tumo-emulator-test'), host='Linux')
        self.assertIn('sourceUnavailable', result['blockers'])
        self.assertFalse(result['readyToRun'])


if __name__ == '__main__': unittest.main()
