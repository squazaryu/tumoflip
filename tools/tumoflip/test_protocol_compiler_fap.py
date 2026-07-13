#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/protocol_compiler"
PROFILE = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/protocol_compiler/profiles/demo_pulse_pair.tproto"
)
CAPTURE = (
    REPO_ROOT
    / "tools/tumoflip/sd_resources/apps_data/protocol_compiler/demo/validation.sub"
)


class ProtocolCompilerFapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (APP_ROOT / "protocol_compiler.c").read_text(encoding="utf-8")
        cls.core = (APP_ROOT / "protocol_profile_core.c").read_text(encoding="utf-8")
        cls.storage = (APP_ROOT / "protocol_profile_storage.c").read_text(encoding="utf-8")
        cls.manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")

    def test_manifest_cockpit_and_package_routes(self) -> None:
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('appid="protocol_compiler"', self.manifest)
        self.assertIn('fap_category="Module One/Signals"', self.manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Signals/protocol_compiler.fap"',
            self.manifest,
        )
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_ROOT / "icon.png").is_file())
        self.assertIn('"Signals: Compiler"', cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Signals/protocol_compiler.fap")', cockpit
        )
        for route in (
            "apps/Module One/Signals/protocol_compiler.fap",
            "apps_data/protocol_compiler/profiles/demo_pulse_pair.tproto",
            "apps_data/protocol_compiler/demo/validation.sub",
        ):
            self.assertIn(f'"{route}"', validator)

    def test_fap_is_receive_only_and_has_human_controls(self) -> None:
        for label in ("Prev", "Open", "Next", "Back", "Select", "Info", "Again"):
            self.assertIn(f'"{label}"', self.source)
        self.assertIn("dialog_file_browser_show", self.source)
        self.assertIn("protocol_profile_decode", self.source)
        self.assertIn("ProtocolProfileMaximumCapturePulses = 512", (
            APP_ROOT / "protocol_profile_core.h"
        ).read_text(encoding="utf-8"))
        forbidden = (
            "subghz_devices",
            "furi_hal_subghz",
            "subghz_tx",
            "transmit",
            "RadioBroker",
            "furi_hal_power_enable_otg",
        )
        combined = self.source + self.core + self.storage
        for symbol in forbidden:
            self.assertNotIn(symbol, combined)

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


if __name__ == "__main__":
    unittest.main()
