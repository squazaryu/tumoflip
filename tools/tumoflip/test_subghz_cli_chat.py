#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class SubGhzCliChatTest(unittest.TestCase):
    def setUp(self) -> None:
        self.sources = {
            path: (REPO_ROOT / path).read_text(encoding="utf-8")
            for path in (
                "applications/main/subghz/subghz_cli.c",
                "applications_user/arf_subghz_full/subghz_cli.c",
            )
        }

    def test_tx_not_allowed_exit_deinitializes_radio_registry(self) -> None:
        for path, source in self.sources.items():
            with self.subTest(path=path):
                block = source.split(
                    "if(!furi_hal_subghz_is_tx_allowed(frequency)) {", 1
                )[1].split("return;", 1)[0]

                self.assertIn("subghz_devices_deinit();", block)
                self.assertIn("subghz_cli_radio_device_power_off();", block)

    def test_chat_worker_stops_before_subghz_devices_deinit(self) -> None:
        for path, source in self.sources.items():
            with self.subTest(path=path):
                shutdown = source.split("furi_string_free(sysmsg);", 1)[1].split(
                    'printf("\\r\\nExit chat\\r\\n");',
                    1,
                )[0]

                worker_stop = shutdown.index("subghz_chat_worker_stop(subghz_chat);")
                worker_free = shutdown.index("subghz_chat_worker_free(subghz_chat);")
                devices_deinit = shutdown.index("subghz_devices_deinit();")
                power_off = shutdown.index("subghz_cli_radio_device_power_off();")

                self.assertLess(worker_stop, worker_free)
                self.assertLess(worker_free, devices_deinit)
                self.assertLess(devices_deinit, power_off)


if __name__ == "__main__":
    unittest.main()
