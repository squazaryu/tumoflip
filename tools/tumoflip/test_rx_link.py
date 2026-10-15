"""Bounded passive Module One wire/backend tests; no device or radio activity."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/tumo_rx_link"


class RxLinkTests(unittest.TestCase):
    def test_backend_archive_rejects_wrong_chip_layout_and_commit(self):
        from tools.tumoflip.esp32_rx_link.package_backend import package
        with tempfile.TemporaryDirectory(prefix="rx-backend-package-") as directory:
            root = Path(directory)
            build = root / "build"
            (build / "bootloader").mkdir(parents=True)
            (build / "partition_table").mkdir()
            (root / "README.md").write_text("Experimental; replaces Marauder")
            files = {"0x1000": "bootloader/bootloader.bin",
                     "0x8000": "partition_table/partition-table.bin",
                     "0x10000": "tumo_rx_link_module_one.bin"}
            image = bytes([0xE9]) + bytes(23)
            for path in files.values():
                (build / path).write_bytes(image)
            args = {"flash_files": files, "extra_esptool_args": {"chip": "esp32"}}
            config = build / "flasher_args.json"
            config.write_text(json.dumps(args))
            with self.assertRaises(ValueError):
                package(root, "branch-name")
            args["extra_esptool_args"]["chip"] = "esp32c5"
            config.write_text(json.dumps(args))
            with self.assertRaises(ValueError):
                package(root, "a" * 40)
            args["extra_esptool_args"]["chip"] = "esp32"
            config.write_text(json.dumps(args))
            package(root, "a" * 40)
            archive = build / "tumo-rx-link-module-one-0.1.0.zip"
            first = archive.read_bytes()
            package(root, "a" * 40)
            self.assertEqual(archive.read_bytes(), first)
            with zipfile.ZipFile(archive) as z:
                metadata = json.loads(z.read("rx-link-build.json"))
                self.assertFalse(metadata["hardware_accepted"])
                self.assertTrue(metadata["replaces_marauder"])
                self.assertEqual(metadata["board"], "module-one-v6.1")
                self.assertEqual(len(z.namelist()), 5)
            (build / "tumo_rx_link_module_one.bin").write_bytes(b"bad image")
            with self.assertRaises(ValueError):
                package(root, "a" * 40)

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
