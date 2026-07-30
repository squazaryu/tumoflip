#!/usr/bin/env python3
"""Regression contracts for selected Sub-GHz integrations from Unleashed 090."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class SubGhzUnleashed090Test(unittest.TestCase):
    def test_pecinin_uses_buttonless_fixed_part(self) -> None:
        keeloq = source("lib/subghz/protocols/keeloq.c")
        self.assertIn(
            'if(strcmp(instance->manufacture_name, "Pecinin") == 0)', keeloq
        )
        self.assertIn("fix = instance->generic.serial;", keeloq)
        self.assertIn("subghz_protocol_keeloq_check_decrypt_pecinin(", keeloq)
        self.assertIn("instance->btn = decrypt >> 28;", keeloq)
        self.assertIn(
            'if(strcmp(*manufacture_name, "Pecinin") != 0)', keeloq
        )

    def test_elplast_is_registered_for_decode_and_send(self) -> None:
        protocol = source("lib/subghz/protocols/elplast.c")
        registry = source("lib/subghz/protocols/protocol_items.c")
        self.assertIn("&subghz_protocol_elplast", registry)
        self.assertIn("SubGhzProtocolFlag_Decodable", protocol)
        self.assertIn("SubGhzProtocolFlag_Send", protocol)
        self.assertIn(
            "subghz_protocol_decoder_elplast_feed(void* context", protocol
        )
        self.assertIn(
            "subghz_protocol_encoder_elplast_deserialize(", protocol
        )

    def test_fm12k_preset_reaches_internal_and_external_cc1101(self) -> None:
        preset_header = source("lib/subghz/devices/preset.h")
        config = source("lib/subghz/devices/cc1101_configs.c")
        internal = source(
            "lib/subghz/devices/cc1101_int/cc1101_int_interconnect.c"
        )
        external = source(
            "applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.c"
        )
        settings = source("lib/subghz/subghz_setting.c")

        self.assertIn("FuriHalSubGhzPreset2FSKDev12KAsync", preset_header)
        self.assertIn(
            "subghz_device_cc1101_preset_2fsk_dev12khz_async_regs", config
        )
        self.assertIn("FuriHalSubGhzPreset2FSKDev12KAsync", internal)
        self.assertIn("FuriHalSubGhzPreset2FSKDev12KAsync", external)
        self.assertIn('"FM12K"', settings)

    def test_cardin_s449_uses_fm12k_and_keeloq_manufacturer(self) -> None:
        generator = source("applications/main/subghz/helpers/subghz_gen_info.c")
        keeloq = source("lib/subghz/protocols/keeloq.c")

        cardin_case = generator[
            generator.index("case SetTypeCardinS449_433FM") :
            generator.index("case SetTypePujol433")
        ]
        self.assertIn('.mod = "FM12K"', cardin_case)
        self.assertIn('.keeloq.manuf = "Cardin_S449"', cardin_case)
        self.assertIn('"Cardin_S449"', keeloq)


if __name__ == "__main__":
    unittest.main()
