#!/usr/bin/env python3
"""Regression contracts for Nice O-Code Add Manually support."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class NiceOCodeAddManuallyTest(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_generator_type_is_registered_without_replacing_existing_variants(self) -> None:
        events = self.read("applications/main/subghz/helpers/subghz_custom_event.h")
        menu = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_type.c")
        generator = self.read("applications/main/subghz/plugins/add_manually/subghz_gen_info.c")
        info = self.read("applications/main/subghz/helpers/subghz_gen_info.h")

        self.assertIn("SetTypeNiceOCode_433_92", events)
        self.assertIn('[SetTypeNiceOCode_433_92] = "Nice O-Code 433MHz"', menu)
        self.assertIn(".nice_flor_s.o_code = true", generator)
        self.assertIn(".nice_flor_s.nice_one = false", generator)
        self.assertIn(".nice_flor_s.ic =", generator)
        self.assertRegex(info, re.compile(r"uint16_t\s+ic;[\s\S]*bool\s+o_code;"))
        self.assertIn(".nice_flor_s.o_code = false", generator)
        self.assertIn(".nice_flor_s.nice_one = true", generator)

    def test_advanced_flow_prompts_for_a_16_bit_installer_code(self) -> None:
        counter = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_counter.c")
        seed = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_seed.c")
        type_scene = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_type.c")

        self.assertRegex(
            type_scene,
            re.compile(r"case GenNiceFlorS:[\s\S]{0,180}nice_flor_s\.o_code"),
        )
        self.assertRegex(
            counter,
            re.compile(
                r"case GenNiceFlorS:[\s\S]{0,260}nice_flor_s\.o_code[\s\S]{0,180}"
                r"SubGhzSceneSetSeed"
            ),
        )
        self.assertIn("byte_count = sizeof(subghz->gen_info->nice_flor_s.ic)", seed)
        self.assertIn("__bswap16", seed)
        self.assertIn('"Enter IC in hex"', seed)
        self.assertIn("subghz_txrx_gen_nice_o_code_protocol", seed)

    def test_core_encoder_adds_o_code_without_changing_flor_s_api(self) -> None:
        protocol = self.read("lib/subghz/protocols/nice_flor_s.c")
        api = self.read("lib/subghz/protocols/public_api.h")
        symbols = self.read("targets/f7/api_symbols.csv")
        plugin_api = self.read("applications/main/subghz/helpers/subghz_feature_plugin.h")

        self.assertIn("bool subghz_protocol_nice_o_create_data(", api)
        self.assertIn("bool subghz_protocol_nice_flor_s_create_data(", api)
        self.assertRegex(protocol, re.compile(r"subghz_protocol_nice_o_create_data\([\s\S]+?\n}"))
        self.assertIn('flipper_format_insert_or_update_hex(flipper_format, "IC"', protocol)
        self.assertIn("SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE", protocol)
        self.assertEqual(symbols.splitlines()[1], "Version,+,88.19,,")
        self.assertIn("subghz_protocol_nice_o_create_data", symbols)
        self.assertIn("SUBGHZ_FEATURE_PLUGIN_API_VERSION 1003", plugin_api)

    def test_add_manually_uses_new_encoder_only_for_o_code(self) -> None:
        type_scene = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_type.c")
        counter = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_counter.c")
        wrapper = self.read("applications/main/subghz/plugins/add_manually/subghz_txrx_create_protocol_key.c")

        self.assertIn("subghz_txrx_gen_nice_o_code_protocol", type_scene + counter)
        self.assertIn("subghz_protocol_nice_flor_s_create_data", wrapper)
        self.assertIn("subghz_protocol_nice_o_create_data", wrapper)

    def test_advanced_generator_rejects_values_outside_protocol_widths(self) -> None:
        serial = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_serial.c")
        button = self.read("applications/main/subghz/plugins/add_manually/subghz_scene_set_button.c")

        self.assertIn('"Serial exceeds 28-bit Nice O-Code range."', serial)
        self.assertIn('"Button must fit in 4 bits for Nice O-Code."', button)


if __name__ == "__main__":
    unittest.main()
