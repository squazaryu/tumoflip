"""Bounded passive Module One wire/backend tests; no device or radio activity."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/tumo_rx_link"


class RxLinkTests(unittest.TestCase):
    def test_application_and_esp32_are_receive_only(self):
        front = APP / "tumo_rx_link.c"
        back = ROOT / "tools/tumoflip/esp32_rx_link/main/main.c"
        self.assertTrue(front.is_file(), "Flipper RX frontend missing")
        self.assertTrue(back.is_file(), "Module One backend missing")
        source = front.read_text()
        self.assertIn("rx_link_client_accept", source)
        self.assertIn("subghz_app_radio_acquire", source)
        self.assertIn("subghz_app_radio_release", source)
        self.assertIn("subghz_devices_stop_async_rx", source)
        self.assertIn("expansion_disable", source)
        self.assertIn("expansion_enable", source)
        for prohibited in ("start_async_tx", "set_tx(", "write_packet(",
                           "storage_file_remove", "storage_file_write", "FW_BEGIN"):
            self.assertNotIn(prohibited, source)
        self.assertIn("CONFIG_IDF_TARGET_ESP32", back.read_text())
        self.assertIn("RX_LINK_BAUD", source)
        self.assertIn("RX_LINK_BAUD", back.read_text())

    def test_wire_and_backend(self):
        with tempfile.TemporaryDirectory(prefix="tumo-rx-link-") as directory:
            executable = Path(directory) / "rx_link_test"
            source = APP / "rx_link.c"
            # An absent implementation intentionally produces undefined references.
            production = [str(source)] if source.exists() else []
            built = subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(APP),
                str(ROOT / "tools/tumoflip/fixtures/rx_link_test.c"), *production,
                "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
