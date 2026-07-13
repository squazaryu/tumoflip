#!/usr/bin/env python3

import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumonet_bench"


class TumoNetBenchTest(unittest.TestCase):
    def test_manifest_and_security_boundary(self) -> None:
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")
        source = (APP_DIR / "tumonet_bench.c").read_text(encoding="utf-8")
        radio = (APP_DIR / "radio/tumonet_radio.c").read_text(encoding="utf-8")
        docs = (REPO_ROOT / "docs/tumonet-phase-a.md").read_text(encoding="utf-8")

        self.assertIn('appid="tumonet_bench"', manifest)
        self.assertIn('fap_libs=["mbedtls"]', manifest)
        self.assertIn('fap_dist_path="apps/Module One/Sub-GHz/tumonet_bench.fap"', manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())
        self.assertIn("RAM only", source)
        self.assertIn("secret_material=not_exported", source)
        self.assertIn("SubGhzRadioBrokerDeviceDual", radio)
        self.assertIn("FuriHalSubGhzPresetCustom", radio)
        self.assertIn("does not", docs)
        self.assertIn("independent-node", docs)

    def test_core_crypto_and_protocol_on_host(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tumonet-core-") as temp_dir:
            executable = Path(temp_dir) / "tumonet_core_host"
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                '-DMBEDTLS_CONFIG_FILE="mbedtls_cfg.h"',
                f"-I{REPO_ROOT / 'lib'}",
                f"-I{REPO_ROOT / 'lib/mbedtls/include'}",
                str(APP_DIR / "tests/tumonet_core_host.c"),
                str(APP_DIR / "core/tumonet_crypto.c"),
                str(APP_DIR / "core/tumonet_protocol.c"),
                str(REPO_ROOT / "lib/mbedtls/library/aes.c"),
                str(REPO_ROOT / "lib/mbedtls/library/md.c"),
                str(REPO_ROOT / "lib/mbedtls/library/md5.c"),
                str(REPO_ROOT / "lib/mbedtls/library/platform_util.c"),
                str(REPO_ROOT / "lib/mbedtls/library/sha1.c"),
                str(REPO_ROOT / "lib/mbedtls/library/sha256.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, cwd=REPO_ROOT, check=True, capture_output=True, text=True)
            completed = subprocess.run(
                [str(executable)], cwd=REPO_ROOT, check=True, capture_output=True, text=True
            )
            self.assertEqual(completed.stdout.strip(), "tumonet core: ok")


if __name__ == "__main__":
    unittest.main()
