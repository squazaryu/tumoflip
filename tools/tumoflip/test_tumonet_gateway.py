#!/usr/bin/env python3

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumonet_gateway"
COCKPIT = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"


class TumoNetGatewayTest(unittest.TestCase):
    def test_manifest_and_product_route(self) -> None:
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")
        source = (APP_DIR / "tumonet_gateway.c").read_text(encoding="utf-8")
        cockpit = COCKPIT.read_text(encoding="utf-8")

        self.assertIn('appid="tumonet_gateway"', manifest)
        self.assertIn('fap_dist_path="apps/Module One/Network/tumonet_gateway.fap"', manifest)
        self.assertIn('fap_libs=["mbedtls"]', manifest)
        self.assertIn('"tumonet"', source)
        self.assertIn('"capabilities"', source)
        self.assertIn('"status"', source)
        self.assertIn('"send"', source)
        self.assertIn("TUMONET_GATEWAY_TEXT_MAX", source)
        self.assertIn("tumonet_gateway_seen_contains", source)
        self.assertIn("Network: TumoNet", cockpit)
        self.assertIn("tumonet_gateway.fap", cockpit)

        manifest_version = re.search(r'fap_version="([^"]+)"', manifest)
        source_version = re.search(r'#define TUMONET_GATEWAY_VERSION\s+"([^"]+)"', source)
        self.assertIsNotNone(manifest_version)
        self.assertIsNotNone(source_version)
        self.assertEqual(manifest_version.group(1), source_version.group(1))

    def test_protocol_codec_on_host(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tumonet-gateway-") as temp_dir:
            executable = Path(temp_dir) / "tumonet_gateway_protocol_host"
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(APP_DIR / "tests/tumonet_gateway_protocol_host.c"),
                str(APP_DIR / "tumonet_gateway_protocol.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, cwd=REPO_ROOT, check=True, capture_output=True, text=True)
            completed = subprocess.run(
                [str(executable)], cwd=REPO_ROOT, check=True, capture_output=True, text=True
            )
            self.assertEqual(completed.stdout.strip(), "tumonet gateway protocol: ok")


if __name__ == "__main__":
    unittest.main()
