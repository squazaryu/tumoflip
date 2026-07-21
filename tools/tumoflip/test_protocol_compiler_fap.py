#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/signal_workbench"
PROFILE = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/signal_workbench/profiles/demo_pulse_pair.tproto"
)
CAPTURE = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/signal_workbench/demo/validation.sub"
)


class ProtocolProfilesIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (APP_ROOT / "signal_workbench.c").read_text(encoding="utf-8")
        cls.runtime = (APP_ROOT / "tumospectrum_protocol_runtime.c").read_text(
            encoding="utf-8"
        )
        cls.core = (APP_ROOT / "protocol_profile_core.c").read_text(encoding="utf-8")
        cls.storage = (APP_ROOT / "protocol_profile_storage.c").read_text(
            encoding="utf-8"
        )
        cls.storage_header = (APP_ROOT / "protocol_profile_storage.h").read_text(
            encoding="utf-8"
        )
        cls.builder = (APP_ROOT / "tumospectrum_profile_builder.c").read_text(
            encoding="utf-8"
        )
        cls.manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")

    def test_standalone_fap_is_replaced_by_tumospectrum_route(self) -> None:
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        self.assertFalse((REPO_ROOT / "applications_user/protocol_compiler").exists())
        self.assertIn('appid="signal_workbench"', self.manifest)
        self.assertIn('"subghz_radio_broker"', self.manifest)
        self.assertIn('"Signals: Profiles"', cockpit)
        self.assertIn('"tumospectrum_profiles"', cockpit)
        self.assertNotIn('EXT_PATH("apps/Module One/Signals/protocol_compiler.fap")', cockpit)
        self.assertIn(
            '"apps_data/signal_workbench/profiles/demo_pulse_pair.tproto"',
            validator,
        )
        self.assertIn(
            '"apps_data/signal_workbench/demo/validation.sub"', validator
        )
        self.assertIn(
            '"subghz/TumoSpectrum Demo/train_0.sub"',
            validator,
        )
        self.assertIn(
            '"/ext/apps/Module One/Signals/protocol_compiler.fap"', validator
        )

    def test_live_runtime_is_bounded_receive_only_and_broker_owned(self) -> None:
        for required in (
            "ProtocolProfileMaximumCapturePulses",
            "TUMOSPECTRUM_PROTOCOL_STREAM_ITEMS",
            "furi_stream_buffer_send",
            "furi_stream_buffer_receive",
            "subghz_radio_broker_acquire",
            "subghz_devices_start_async_rx",
            "subghz_devices_stop_async_rx",
            "SubGhzRadioBrokerStateCleaningUp",
            "protocol_profile_decode",
        ):
            self.assertIn(required, self.runtime)
        for forbidden in (
            "subghz_devices_start_async_tx",
            "furi_hal_subghz_start_async_tx",
            "subghz_transmitter",
            "furi_hal_power_enable_otg",
        ):
            self.assertNotIn(forbidden, self.runtime)
        self.assertIn('elements_button_left(canvas, "Back")', self.source)
        self.assertIn(
            'elements_button_center(canvas, listening ? "Stop" : "Start")',
            self.source,
        )
        self.assertIn('elements_button_right(canvas, "Demo")', self.source)
        self.assertIn("tumospectrum_protocol_has_demo(package)", self.source)
        self.assertIn("protocol_profile_observation_append", self.source)

    def test_profile_storage_migrates_without_deleting_user_data(self) -> None:
        self.assertIn('EXT_PATH("apps_data/signal_workbench")', self.storage_header)
        self.assertIn('EXT_PATH("apps_data/protocol_compiler")', self.storage_header)
        self.assertIn("protocol_profile_migrate_legacy_profiles", self.storage)
        self.assertIn("storage_common_copy", self.storage)
        self.assertNotIn(
            "storage_common_remove(storage, PROTOCOL_PROFILE_LEGACY", self.storage
        )
        self.assertIn("PROTOCOL_PROFILE_OBSERVATIONS_MAX_SIZE", self.storage)
        self.assertIn("protocol_observations.previous.csv", self.storage)
        self.assertIn("value < 0x20U || value > 0x7EU || value == '\"'", self.storage)

    def test_on_device_builder_is_bounded_atomic_and_receive_only(self) -> None:
        self.assertIn('"Create Live Profile"', self.source)
        self.assertIn("tumospectrum_profile_builder_build", self.source)
        self.assertIn("protocol_profile_package_save", self.source)
        self.assertIn("TUMOSPECTRUM_SET_MIN_SAMPLES", self.builder)
        self.assertIn("ProtocolProfileMaximumBits", self.builder)
        self.assertIn(".receive_only = true", self.builder)
        self.assertIn(".review_required = true", self.builder)
        self.assertIn("storage_common_rename(storage, temporary, path)", self.storage)
        for forbidden in (
            "start_async_tx",
            "subghz_transmitter",
            "furi_hal_subghz_start_async_tx",
        ):
            self.assertNotIn(forbidden, self.builder)

    def test_demo_artifacts_are_deterministic_and_validate(self) -> None:
        subprocess.run(
            [
                str(REPO_ROOT / "toolchain/arm64-darwin/bin/python3"),
                "tools/tumoflip/generate_protocol_demo.py",
                "--check",
            ],
            cwd=REPO_ROOT,
            check=True,
        )
        result = subprocess.run(
            [
                str(REPO_ROOT / "toolchain/arm64-darwin/bin/python3"),
                "tools/tumoflip/protocol_compiler.py",
                "validate",
                "--profile",
                str(PROFILE),
                str(CAPTURE),
            ],
            cwd=REPO_ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        )
        self.assertIn("0xA78 (13 bits)", result.stdout)

    def test_portable_core_with_sanitizers(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "protocol_profile_core_host_test"
            build = subprocess.run(
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
                    str(APP_ROOT / "protocol_profile_core.c"),
                    str(REPO_ROOT / "tools/tumoflip/protocol_profile_core_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(build.returncode, 0, build.stdout)
            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            run = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(run.returncode, 0, run.stdout)
            self.assertIn("protocol_profile_core_host_test: PASS", run.stdout)

    def test_portable_profile_builder_with_sanitizers(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "tumospectrum_profile_builder_host_test"
            build = subprocess.run(
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
                    str(APP_ROOT / "protocol_profile_core.c"),
                    str(APP_ROOT / "tumospectrum_profile_builder.c"),
                    str(REPO_ROOT / "tools/tumoflip/tumospectrum_profile_builder_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(build.returncode, 0, build.stdout)
            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            run = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(run.returncode, 0, run.stdout)
            self.assertIn("tumospectrum_profile_builder_host_test: PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
