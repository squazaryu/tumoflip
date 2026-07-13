#!/usr/bin/env python3

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CORE_ROOT = REPO_ROOT / "applications/services/tumoflip_runtime"


class TumoFabricTest(unittest.TestCase):
    def test_portable_core_contract_under_sanitizers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "tumofabric_host_test"
            command = [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I",
                str(CORE_ROOT),
                str(CORE_ROOT / "tumofabric_core.c"),
                str(REPO_ROOT / "tools/tumoflip/tumofabric_host_test.c"),
                "-o",
                str(binary),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )
            self.assertIn("tumofabric_host_test: PASS", result.stdout)

    def test_runtime_exposes_bounded_fabric_commands(self) -> None:
        source = (CORE_ROOT / "tumoflip_runtime.c").read_text(encoding="utf-8")
        header = (CORE_ROOT / "tumoflip_runtime.h").read_text(encoding="utf-8")

        for command in (
            "fabric_caps",
            "fabric_open",
            "fabric_state",
            "fabric_step",
            "fabric_cancel",
        ):
            self.assertIn(f'"{command}"', source)
        self.assertIn("fabric=1", source)
        self.assertIn("transfer,fabric", source)
        self.assertIn("persist=ram", source)
        self.assertIn("trust=ble-bond", source)
        self.assertIn("get_fabric_state", header)
        self.assertIn("cancel_fabric", header)


if __name__ == "__main__":
    unittest.main()
