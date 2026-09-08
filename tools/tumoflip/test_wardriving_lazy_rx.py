"""Host-executed lifecycle regression tests using actual Wardriving functions."""

import hashlib
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/subghz_wardriving"


def function(source, name):
    match = re.search(r"^[\w *\n]+\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise AssertionError(f"Missing lifecycle function: {name}")
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class WardrivingLazyRxTest(unittest.TestCase):
    def test_menu_allocation_does_not_create_decoders_or_worker(self):
        source = (APP / "helpers/subghz_wardriving_txrx.c").read_text()
        alloc = function(source, "subghz_wardriving_txrx_alloc")
        for allocation in ("subghz_worker_alloc(", "subghz_receiver_alloc_init(",
                           "subghz_environment_alloc("):
            self.assertNotIn(allocation, alloc)

    def test_actual_pipeline_lifecycle(self):
        source = (APP / "helpers/subghz_wardriving_txrx.c").read_text()
        names = [
            "subghz_wardriving_txrx_environment_ensure",
            "subghz_wardriving_txrx_rx_pipeline_alloc",
            "subghz_wardriving_txrx_rx_pipeline_release",
            "subghz_wardriving_txrx_is_database_loaded",
            "subghz_wardriving_txrx_receiver_set_filter",
            "subghz_wardriving_txrx_set_rx_callback",
            "subghz_wardriving_txrx_get_receiver",
            "subghz_wardriving_txrx_rx_end",
        ]
        actual = "\n\n".join(function(source, name) for name in names)
        fixture = (ROOT / "tools/tumoflip/tests/wardriving_lazy_rx_host.c").read_text()
        with tempfile.TemporaryDirectory(prefix="wardriving-rx-") as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(fixture.replace("/* ACTUAL_FUNCTIONS */", actual))
            coverage = os.environ.get("WARDRIVING_HOST_COVERAGE") == "1"
            flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if coverage else []
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", *flags,
                                     str(path / "test.c"), "-o", str(path / "test")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True,
                                    env={**os.environ, "LLVM_PROFILE_FILE": str(path / "test.profraw")})
            self.assertEqual(result.returncode, 0, result.stderr)
            if coverage:
                subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse",
                                str(path / "test.profraw"), "-o", str(path / "test.profdata")], check=True)
                subprocess.run(["xcrun", "llvm-cov", "report", str(path / "test"),
                                "-instr-profile=" + str(path / "test.profdata"),
                                "-show-functions", "-name-regex=subghz_wardriving_txrx_",
                                str(path / "test.c")], check=True)

    def test_release_points_and_stop_order(self):
        source = (APP / "helpers/subghz_wardriving_txrx.c").read_text()
        for relative, name in [
            ("scenes/subghz_wardriving_scene_start.c", "subghz_scene_start_on_enter"),
            ("subghz_wardriving_i.c", "subghz_load_protocol_from_file"),
        ]:
            body = function((APP / relative).read_text(), name)
            self.assertIn("subghz_wardriving_txrx_rx_pipeline_release(", body)
        body = function(source, "subghz_wardriving_txrx_free")
        self.assertLess(body.index("subghz_wardriving_txrx_stop("),
                        body.index("subghz_wardriving_txrx_rx_pipeline_release("))
        self.assertLess(body.index("subghz_wardriving_txrx_rx_pipeline_release("),
                        body.index("subghz_devices_deinit("))

    def test_signal_entry_points_restore_pipeline(self):
        source = (APP / "helpers/subghz_wardriving_txrx.c").read_text()
        for name in ("rx_start", "tx_start", "load_decoder_by_name_protocol", "get_receiver"):
            self.assertIn("subghz_wardriving_txrx_rx_pipeline_alloc(instance)",
                          function(source, "subghz_wardriving_txrx_" + name))

    def test_gps_and_worker_unchanged(self):
        # Scope guard from b7ee21eb; no git history required in shallow CI clones.
        expected = {
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_gps_plugin.c":
                "da2f9063107ab12547844b1bc4ab9d32f70ac6332d2a3565affe738a81f5f065",
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_gps.c":
                "7ae068874a5e0841ca8685c8f1aa21fe578456cd26c3dccca61a30d93e3bcf89",
            "lib/subghz/subghz_worker.c":
                "8e2700e262d32de4df948ed447bd1fef8b056135e5020ec0ae3ecd79c76610ed",
        }
        for relative, digest in expected.items():
            self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
