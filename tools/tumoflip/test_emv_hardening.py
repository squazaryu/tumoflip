#!/usr/bin/env python3
"""Regression contracts for the EMV parser and saved-file bounds."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class EmvHardeningTest(unittest.TestCase):
    def test_saved_file_lengths_are_bounded_before_hex_reads(self) -> None:
        emv = source("lib/nfc/protocols/emv/emv.c")
        self.assertIn('emv_load_length(ff, "PAN length", sizeof(app->pan)', emv)
        self.assertIn('emv_load_length(ff, "AID length", sizeof(app->aid)', emv)
        self.assertNotIn("strcpy(app->", emv)

    def test_saved_file_strings_are_bounded_and_pin_width_is_safe(self) -> None:
        emv = source("lib/nfc/protocols/emv/emv.c")
        self.assertIn("strlcpy(dest, furi_string_get_cstr(value), dest_size);", emv)
        self.assertIn("uint32_t pin_try_counter = app.pin_try_counter;", emv)
        self.assertNotIn("(uint32_t*)&app.pin_try_counter", emv)

    def test_card_tlv_headers_and_values_are_checked(self) -> None:
        poller = source("lib/nfc/protocols/emv/emv_poller_i.c")
        for contract in (
            "if(i >= len) return success;",
            "if(i + 1 >= len) return success;",
            "if((uint16_t)i + tlen > len)",
            "if(tlen > sizeof(app->pan))",
            "if(tlen >= sizeof(app->cardholder_name))",
            "if(length_bytes != 1U)",
            "if(len == 0) return true;",
        ):
            self.assertIn(contract, poller)

    def test_fixed_width_card_fields_reject_truncation(self) -> None:
        poller = source("lib/nfc/protocols/emv/emv_poller_i.c")
        for contract in (
            "tlen != EMV_BCD_DATE_SIZE",
            "tlen != EMV_BCD_TIME_SIZE",
            "tlen != EMV_BCD_AMOUNT_SIZE",
            "tlen != sizeof(app->currency_code)",
            "tlen != sizeof(app->country_code)",
            "tlen != sizeof(app->transaction_counter)",
            "tlen != sizeof(app->pin_try_counter)",
        ):
            self.assertIn(contract, poller)

    def test_pdol_and_transaction_log_are_bounded(self) -> None:
        poller = source("lib/nfc/protocols/emv/emv_poller_i.c")
        self.assertIn("EMV_PDOL_MAX_SIZE", poller)
        self.assertRegex(
            poller,
            re.compile(r"uint8_t copy_len = MIN\(tlen, pdol_values\[j\]->size\);"),
        )
        self.assertIn("emv_trans_writable(app)", poller)
        self.assertIn("COUNT_OF(instance->data->emv_application.trans)", poller)

    def test_unlshd_092_removes_unreachable_emv_exports(self) -> None:
        render = source("applications/main/nfc/helpers/protocol_support/emv/emv_render.c")
        header = source("applications/main/nfc/helpers/protocol_support/emv/emv_render.h")
        nfc_app = source("applications/main/nfc/nfc_app.c")
        nfc_app_i = source("applications/main/nfc/nfc_app_i.h")
        classic_i = source("lib/nfc/protocols/mf_classic/mf_classic_poller_i.h")
        self.assertNotIn("nfc_render_emv_data", render + header)
        self.assertNotIn("nfc_render_emv_pan", render + header)
        self.assertNotIn("nfc_render_emv_name", render + header)
        self.assertIn("static void nfc_make_app_folders", nfc_app)
        self.assertNotIn("nfc_make_app_folder", nfc_app_i)
        self.assertNotIn("nfc_task(void* p)", nfc_app_i)
        for symbol in ("auth1_backdoor_key", "auth2_backdoor_key", "auth3_backdoor_key"):
            self.assertNotIn(symbol, classic_i)

    def test_native_emv_fixtures_cover_valid_and_rejected_files(self) -> None:
        unit_test = source("applications/debug/unit_tests/tests/nfc/nfc_test.c")
        for fixture in (
            "Emv_valid.nfc",
            "Emv_long_strings.nfc",
            "Emv_oversized_pan.nfc",
            "Emv_oversized_aid.nfc",
            "Emv_missing_aid.nfc",
        ):
            self.assertIn(fixture, unit_test)
            self.assertTrue(
                (
                    REPO_ROOT
                    / "applications/debug/unit_tests/resources/unit_tests/nfc"
                    / fixture
                ).is_file()
            )
        self.assertIn("MU_RUN_TEST(emv_file_bounds_test);", unit_test)


if __name__ == "__main__":
    unittest.main()
