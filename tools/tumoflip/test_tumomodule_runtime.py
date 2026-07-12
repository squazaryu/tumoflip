#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumomodule_runtime"
PACKAGE_ROOT = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/tumomodule_runtime/modules"
)


def parse_flipper_format(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="ascii").splitlines():
        if not raw_line or raw_line.startswith("#"):
            continue
        key, value = raw_line.split(":", 1)
        values[key.strip()] = value.strip()
    return values


class TumoModuleRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.runtime_source = (APP_ROOT / "tumomodule_runtime.c").read_text(
            encoding="utf-8"
        )
        cls.driver_source = (APP_ROOT / "tumomodule_drivers.c").read_text(
            encoding="utf-8"
        )
        cls.manifest_source = (APP_ROOT / "application.fam").read_text(
            encoding="utf-8"
        )

    def test_reference_packages_are_bounded_api_88_manifests(self) -> None:
        expected = {
            "bme280.tmod": ("bme280_i2c", "i2c.external"),
            "tumovgm.tmod": ("tumovgm_uart", "uart.usart"),
        }
        self.assertEqual({path.name for path in PACKAGE_ROOT.glob("*.tmod")}, set(expected))
        for filename, (adapter, bus) in expected.items():
            values = parse_flipper_format(PACKAGE_ROOT / filename)
            self.assertEqual(values["Filetype"], "TumoModule Package")
            self.assertEqual(values["Version"], "1")
            self.assertEqual(values["Adapter"], adapter)
            self.assertEqual(values["Bus"], bus)
            self.assertEqual(values["Minimum API"], "88")
            self.assertEqual(values["Maximum API"], "88")
            self.assertEqual(values["External Power"], "false")

    def test_fap_is_packaged_with_human_ui_and_bounded_probes(self) -> None:
        self.assertIn('appid="tumomodule_runtime"', self.manifest_source)
        self.assertIn(
            'fap_dist_path="apps/Module One/Modules/tumomodule_runtime.fap"',
            self.manifest_source,
        )
        self.assertIn('fap_icon="../../applications/main/gpio/icon.png"', self.manifest_source)
        self.assertIn('elements_button_center(canvas, "Probe")', self.runtime_source)
        self.assertIn('elements_button_left(canvas, "Back")', self.runtime_source)
        self.assertIn('model->about ? "Module" : "Info"', self.runtime_source)
        self.assertIn("tumomodule_resource_claim", self.runtime_source)
        self.assertIn("tumomodule_resource_release", self.runtime_source)
        self.assertIn("TumoModuleVgmTimeoutMs = 600", self.driver_source)
        self.assertIn("TumoModuleI2cTimeoutMs = 5", self.driver_source)
        self.assertIn("TumoModuleBmeMeasureTimeoutMs = 30", self.driver_source)
        self.assertIn("TumovgmMessageImuInfo", self.driver_source)
        self.assertIn('"VGM IMU ID %02X health %u"', self.driver_source)
        self.assertIn("tumomodule_bme280_sample_temperature", self.driver_source)
        self.assertIn('"BME280 %s%lu.%02lu C"', self.driver_source)
        self.assertIn("furi_hal_serial_async_rx_stop(serial)", self.driver_source)
        self.assertIn("furi_hal_serial_control_release(serial)", self.driver_source)
        self.assertIn("furi_hal_i2c_release(&furi_hal_i2c_handle_external)", self.driver_source)
        self.assertNotIn("furi_hal_power_enable_otg", self.driver_source)
        self.assertNotIn("furi_hal_power_disable_otg", self.driver_source)

    def test_cockpit_and_release_validator_route_the_runtime(self) -> None:
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"Modules: Runtime"', cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Modules/tumomodule_runtime.fap")', cockpit
        )
        self.assertIn(
            '"apps/Module One/Modules/tumomodule_runtime.fap"', validator
        )
        for filename in ("bme280.tmod", "tumovgm.tmod"):
            self.assertIn(
                f'"apps_data/tumomodule_runtime/modules/{filename}"', validator
            )

    def test_portable_core_with_sanitizers(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "tumomodule_core_host_test"
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
                    str(APP_ROOT / "tumomodule_core.c"),
                    str(REPO_ROOT / "tools/tumoflip/tumomodule_core_host_test.c"),
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
            self.assertIn("tumomodule_core_host_test: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
