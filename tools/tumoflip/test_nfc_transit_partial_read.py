import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SUPPORTED_CARDS = REPO_ROOT / "applications/main/nfc/plugins/supported_cards"


class NfcTransitPartialReadTest(unittest.TestCase):
    def test_all_targeted_parsers_gate_partial_reads_on_their_data_sector(self) -> None:
        expected_sectors = {
            "plantain.c": "cfg.data_sector",
            "troika.c": "cfg.data_sector",
            "two_cities.c": "4",
            "sevppk_tk.c": "19",
            "szppk_so.c": "19",
        }

        for filename, sector in expected_sectors.items():
            with self.subTest(filename=filename):
                source = (SUPPORTED_CARDS / filename).read_text(encoding="utf-8")
                call = re.compile(
                    rf"mf_classic_is_read_success_for_sector\(\s*error,\s*data,\s*{re.escape(sector)}\s*\)"
                )
                self.assertRegex(source, call)
                self.assertNotIn(
                    "error == MfClassicErrorNone || error == MfClassicErrorPartialRead",
                    source,
                )

    def test_policy_has_a_device_unit_regression(self) -> None:
        header = (
            REPO_ROOT / "lib/nfc/protocols/mf_classic/mf_classic.h"
        ).read_text(encoding="utf-8")
        device_test = (
            REPO_ROOT / "applications/debug/unit_tests/tests/nfc/nfc_test.c"
        ).read_text(encoding="utf-8")

        self.assertIn("mf_classic_is_read_success_for_sector", header)
        self.assertIn("error == MfClassicErrorPartialRead", header)
        self.assertIn("mf_classic_is_sector_read(data, sector_num)", header)
        self.assertIn("mf_classic_partial_read_required_sector_test", device_test)
        self.assertIn("Complete read must be accepted", device_test)
        self.assertIn("Partial read without required sector must be rejected", device_test)
        self.assertIn("Non-read errors must be rejected", device_test)


if __name__ == "__main__":
    unittest.main()
