#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumovm_peripherals"
PACKAGE_ROOT = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/tumovm_peripherals/packages"
)


def parse_flipper_format(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="ascii").splitlines():
        if not raw_line or raw_line.startswith("#"):
            continue
        key, value = raw_line.split(":", 1)
        values[key.strip()] = value.strip()
    return values


class TumoVmPeripheralsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = (APP_ROOT / "tumovm_peripherals.c").read_text(encoding="utf-8")
        cls.core = (APP_ROOT / "tumovm_peripheral_core.c").read_text(
            encoding="utf-8"
        )
        cls.manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")

    def test_reference_packages_are_api88_and_bounded(self) -> None:
        self.assertEqual(
            {path.name for path in PACKAGE_ROOT.glob("*.tper")},
            {"usb_media.tper", "nfc_state.tper"},
        )
        usb = parse_flipper_format(PACKAGE_ROOT / "usb_media.tper")
        nfc = parse_flipper_format(PACKAGE_ROOT / "nfc_state.tper")
        for values in (usb, nfc):
            self.assertEqual(values["Filetype"], "TumoVM Peripheral")
            self.assertEqual(values["Version"], "1")
            self.assertEqual(values["Minimum API"], "88")
            self.assertEqual(values["Maximum API"], "88")
        self.assertEqual(usb["Adapter"], "usb.hid.consumer")
        self.assertEqual(usb["Action"], "play_pause")
        self.assertEqual(usb["Require confirmation"], "true")
        self.assertEqual(nfc["Adapter"], "nfc.type4")
        self.assertEqual(nfc["Action"], "state-v1")
        self.assertEqual(nfc["AID size"], "5")

    def test_fap_has_human_controls_and_cleanup(self) -> None:
        self.assertIn('appid="tumovm_peripherals"', self.manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Labs/tumovm_peripherals.fap"',
            self.manifest,
        )
        for label in (
            "Prev",
            "Start",
            "Next",
            "Stop",
            "Send",
            "Reset",
            "Info",
            "Close",
        ):
            self.assertIn(f'"{label}"', self.app)
        self.assertIn("tumovm_peripheral_resource_claim", self.app)
        self.assertIn("tumovm_peripheral_resource_release", self.app)
        self.assertIn("furi_hal_hid_consumer_key_release_all", self.app)
        self.assertIn("furi_hal_usb_set_config(app->previous_usb, NULL)", self.app)
        self.assertIn("nfc_listener_stop", self.app)
        self.assertIn("tumovm_peripheral_state_save", self.app)
        self.assertIn('"v0.1 / GitHub issue #67"', self.app)
        self.assertNotIn("furi_hal_power_enable_otg", self.app)

    def test_manifest_validator_is_fail_closed(self) -> None:
        self.assertIn("TumoVmPeripheralMaximumPackages = 4", (
            APP_ROOT / "tumovm_peripheral_core.h"
        ).read_text(encoding="utf-8"))
        self.assertIn("TumoVmPeripheralMaximumManifestSize = 1024", (
            APP_ROOT / "tumovm_peripheral_core.h"
        ).read_text(encoding="utf-8"))
        self.assertIn("UntrustedAdapter", self.core)
        self.assertIn("UntrustedAction", self.core)
        self.assertIn("ConfirmationRequired", self.core)
        self.assertIn("input->minimum_api > current_api", self.core)

    def test_cockpit_and_release_package_routes(self) -> None:
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"VM: Peripherals"', cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Labs/tumovm_peripherals.fap")', cockpit
        )
        self.assertIn('"apps/Module One/Labs/tumovm_peripherals.fap"', validator)
        for filename in ("usb_media.tper", "nfc_state.tper"):
            self.assertIn(
                f'"apps_data/tumovm_peripherals/packages/{filename}"', validator
            )

    def test_portable_core_with_sanitizers(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "tumovm_peripheral_core_host_test"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    str(APP_ROOT / "tumovm_peripheral_core.c"),
                    str(REPO_ROOT / "tools/tumoflip/tumovm_peripheral_core_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("tumovm_peripheral_core_host_test: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
