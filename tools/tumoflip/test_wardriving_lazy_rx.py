"""Host-executed lifecycle regression tests using actual Wardriving functions."""

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
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", str(path / "test.c"),
                            "-o", str(path / "test")], check=True, capture_output=True)
            subprocess.run([str(path / "test")], check=True, capture_output=True)

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
        for relative in [
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_gps_plugin.c",
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_gps.c",
            "lib/subghz/subghz_worker.c",
        ]:
            baseline = subprocess.check_output(["git", "show", "b7ee21eb:" + relative], cwd=ROOT)
            self.assertEqual((ROOT / relative).read_bytes(), baseline)


if __name__ == "__main__":
    unittest.main()
