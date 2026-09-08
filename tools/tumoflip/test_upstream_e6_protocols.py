#!/usr/bin/env python3
"""Regression contracts for the staged Unleashed e6ded9b protocol import."""

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROTOCOLS = ROOT / "lib/subghz/protocols"


class UpstreamE6ProtocolsTest(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_nice_o_code_is_a_distinct_variant_with_cached_table(self) -> None:
        source = self.read("lib/subghz/protocols/nice_flor_s.c")
        api = self.read("lib/subghz/protocols/public_api.h")

        for required in (
            "NICE_O_NAME",
            "NICE_FLOR_S_IC_PLAIN",
            "NICE_O_MIN_SAMPLES",
            "subghz_protocol_nice_o_get_parcel",
            "subghz_protocol_nice_o_mask",
            "subghz_protocol_nice_flor_s_decrypt_ic",
            "subghz_protocol_decoder_nice_flor_s_set_skip_o_code",
            "nice_flor_s_table_valid",
            "nice_flor_s_table_path",
        ):
            self.assertIn(required, source + api)
        self.assertRegex(source, re.compile(r"NICE_O_MIN_SAMPLES\s+4"))
        self.assertIn("static char nice_flor_s_table_path[64]", source)
        self.assertIn("flipper_format_read_hex(flipper_format, \"IC\"", source)

    def test_security_plus_keypad_has_bounded_86_bit_path_and_pin(self) -> None:
        source = self.read("lib/subghz/protocols/secplus_v2.c")
        manifest = self.read("applications/system/secplus_pin/application.fam")

        for required in (
            "SECPLUS_V2_86_COUNT_BIT",
            "SECPLUS_V2_ENCODER_UPLOAD_SIZE",
            "subghz_protocol_secplus_v2_decode_half_86",
            "subghz_protocol_secplus_v2_encode_half_86",
            "SECPLUS_V2_PIN_MAX",
            "subghz_protocol_secplus_v2_pin_read",
            "flipper_format_write_string(flipper_format, \"Pin\"",
        ):
            self.assertIn(required, source)
        self.assertIn('appid="secplus_pin"', manifest)
        self.assertIn('entry_point="secplus_pin_app"', manifest)
        self.assertIn("if(size_upload > SECPLUS_V2_ENCODER_UPLOAD_SIZE)", source)

    def test_prastel_42_bit_is_registered_as_rolling_code(self) -> None:
        source = self.read("lib/subghz/protocols/prastel.c")
        header = self.read("lib/subghz/protocols/prastel.h")
        registry = self.read("lib/subghz/protocols/protocol_items.c")
        came = self.read("lib/subghz/protocols/came.c")

        self.assertIn("PRASTEL_42_COUNT_BIT", source)
        self.assertIn("SubGhzProtocolTypeDynamic", source)
        self.assertIn("SUBGHZ_PROTOCOL_PRASTEL_NAME", header)
        self.assertIn("subghz_protocol_prastel", registry)
        self.assertNotIn("PRASTEL_42_COUNT_BIT", came)

    def test_keeloq_learning_types_and_matching_keystore_helpers_exist(self) -> None:
        header = self.read("lib/subghz/protocols/keeloq_common.h")
        source = self.read("lib/subghz/protocols/keeloq_common.c")
        keeloq = self.read("lib/subghz/protocols/keeloq.c")
        keystore = self.read(
            "applications/main/subghz/resources/subghz/assets/keeloq_mfcodes_extended"
        )

        for required in (
            "KEELOQ_LEARNING_JCM_GEN2",
            "KEELOQ_LEARNING_STAGNOLI",
            "KEELOQ_LEARNING_TELCOMA_TABLE_HI",
            "KEELOQ_LEARNING_TELCOMA_TABLE_LO",
            "subghz_protocol_keeloq_common_learning_jcm_gen2",
            "subghz_protocol_keeloq_common_learning_stagnoli",
            "subghz_protocol_keeloq_common_learning_telcoma_table",
            "subghz_protocol_keeloq_common_get_telcoma_table",
        ):
            self.assertIn(required, header + source + keeloq)
        self.assertIn("Encryption: 1", keystore)
        self.assertIn("IV: 54 65 72 72 61 69 6E 21 20 50 75 6C 6C 20 55 70", keystore)
        self.assertEqual(len(keystore.splitlines()), 128)
        self.assertEqual(
            hashlib.sha256(keystore.encode()).hexdigest(),
            "8dbc0959233af4411cf4fa10e0ce09208c30a5b272d48fd5a6189f54dba0372e",
        )

    def test_new_apps_and_api_contract_are_present(self) -> None:
        nice_manifest = self.read("applications/system/nice_o_code/application.fam")
        pin_manifest = self.read("applications/system/secplus_pin/application.fam")
        api = self.read("targets/f7/api_symbols.csv")

        self.assertIn('appid="nice_o_code"', nice_manifest)
        self.assertIn('entry_point="nice_o_code_app"', nice_manifest)
        self.assertIn("Nice O-Code", self.read("applications/system/nice_o_code/nice_o_code.c"))
        self.assertIn("Security+ PIN", self.read("applications/system/secplus_pin/secplus_pin.c"))
        self.assertRegex(api, r"(?m)^Version,\+,88\.6,,$")
        for symbol in (
            "subghz_protocol_nice_o_mask",
            "subghz_protocol_nice_o_get_parcel",
            "subghz_protocol_nice_flor_s_decrypt_ic",
        ):
            self.assertIn(symbol, api)

    def test_shared_subghz_hosts_load_the_extended_keystore(self) -> None:
        for relative in (
            "applications/main/subghz/helpers/subghz_txrx.c",
            "applications_user/arf_subghz_full/helpers/subghz_txrx.c",
            "applications_user/flipper_companion/helpers/subghz_txrx.c",
            "applications_user/quac/actions/helpers/subghz_txrx.c",
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_txrx.c",
            "applications_user/arf_subghz_full/helpers/subghz_keeloq_keys.c",
        ):
            source = self.read(relative)
            self.assertIn("SUBGHZ_KEYSTORE_DIR_EXTENDED", source)
        self.assertIn("keeloq_mfcodes_extended", self.read("applications_user/subghz_raw_edit/subghz_raw_edit.c"))
        self.assertIn("SUBGHZ_KEYSTORE_DIR_EXTENDED", self.read("applications/system/js_app/modules/js_subghz/js_subghz.c"))

    def test_arf_key_editor_accepts_new_learning_types(self) -> None:
        source = self.read("applications_user/arf_subghz_full/scenes/subghz_scene_keeloq_key_edit.c")
        for label in ("16-JCM Gen2", "17-Stagnoli", "18-Telcoma hi", "19-Telcoma lo"):
            self.assertIn(label, source)
        self.assertIn("COUNT_OF(kl_type_options)", source)


if __name__ == "__main__":
    unittest.main()
