import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROTOCOL = ROOT / "lib/subghz/protocols/superrollo.c"
HEADER = ROOT / "lib/subghz/protocols/superrollo.h"
PUBLIC_API = ROOT / "lib/subghz/protocols/public_api.h"
API_SYMBOLS = ROOT / "targets/f7/api_symbols.csv"


def reverse_bits(value: int, count: int) -> int:
    result = 0
    for _ in range(count):
        result = (result << 1) | (value & 1)
        value >>= 1
    return result


def superrollo_crc(word0: int, vlow: int) -> tuple[int, int]:
    crc0 = 0
    crc1 = 0
    for index in range(65):
        input_bit = ((word0 >> index) & 1) if index < 64 else vlow
        next_crc1 = crc0 ^ input_bit
        next_crc0 = next_crc1 ^ crc1
        crc0 = next_crc0 & 1
        crc1 = next_crc1 & 1
    return crc0, crc1


class SuperrolloProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = PROTOCOL.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")

    def test_reference_frame_layout_and_crc(self) -> None:
        # Synthetic vector produced with the shared KeeLoq normal-learning helpers.
        serial = 0x0BADBEEF
        button = 0x3
        encrypted_hop = 0x857AAB06
        word0 = encrypted_hop | (serial << 32) | (button << 60)
        crc0, crc1 = superrollo_crc(word0, 1)
        word1 = 1 | (crc0 << 1) | (crc1 << 2)

        self.assertEqual(word0, 0x3BADBEEF857AAB06)
        self.assertEqual(word1, 0x7)
        self.assertEqual(reverse_bits(word0, 64), 0x60D55EA1F77DB5DC)
        self.assertEqual(reverse_bits(word1, 3), 0x7)

    def test_uses_reviewed_common_keeloq_implementation(self) -> None:
        self.assertIn("subghz_protocol_keeloq_common_normal_learning", self.source)
        self.assertIn("subghz_protocol_keeloq_common_encrypt", self.source)
        self.assertIn("subghz_protocol_keeloq_common_decrypt", self.source)
        self.assertNotIn("memccpy", self.source)
        self.assertNotIn("KEELOQ_NROUNDS", self.source)
        self.assertNotIn("NLF_LOOKUP_CONSTANT", self.source)

    def test_manufacturer_key_lookup_is_exact_and_fail_closed(self) -> None:
        key_lookup = re.search(
            r"static bool subghz_protocol_superrollo_get_manufacturer_key\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(key_lookup)
        lookup = key_lookup.group(0)
        self.assertIn("manufacture_code->type == KEELOQ_LEARNING_NORMAL", lookup)
        self.assertIn("SUPERROLLO_MANUFACTURER_NAME", lookup)
        self.assertIn("return false;", lookup)

    def test_crc_is_checked_before_decoder_callback(self) -> None:
        finish = re.search(
            r"static void subghz_protocol_decoder_superrollo_finish_frame\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(finish)
        self.assertIn("validate_frame(&instance->generic)", finish.group(0))
        self.assertIn("instance->base.callback", finish.group(0))
        self.assertLess(
            finish.group(0).index("validate_frame(&instance->generic)"),
            finish.group(0).index("instance->base.callback"),
        )

    def test_guard_recovers_only_the_missing_final_bit(self) -> None:
        self.assertIn(
            "decode_count_bit == SUPERROLLO_FRAME_BITS - 1U", self.source
        )
        self.assertNotIn("min_count_bit_for_found - 1", self.source)
        self.assertNotIn("Don't infer", self.source)

    def test_decoder_reset_clears_parser_and_frame_state(self) -> None:
        reset = re.search(
            r"static void subghz_protocol_decoder_superrollo_reset_parser\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(reset)
        for state in (
            "decode_data = 0",
            "decode_count_bit = 0",
            "te_last = 0",
            "header_count = 0",
            "generic.data = 0",
            "generic.data_2 = 0",
            "generic.data_count_bit = 0",
        ):
            self.assertIn(state, reset.group(0))

    def test_allocations_are_zero_initialized(self) -> None:
        self.assertEqual(self.source.count("memset(instance, 0"), 2)

    def test_data2_is_cleared_before_deserialization(self) -> None:
        self.assertGreaterEqual(self.source.count("instance->generic.data_2 = 0;"), 3)

    def test_custom_button_map_is_explicit(self) -> None:
        button_map = re.search(
            r"static uint8_t subghz_protocol_superrollo_get_button_code\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(button_map)
        mapping = button_map.group(0)
        self.assertRegex(mapping, r"SUBGHZ_CUSTOM_BTN_UP:\s*return 0x3")
        self.assertRegex(mapping, r"SUBGHZ_CUSTOM_BTN_DOWN:\s*return 0x5")
        self.assertRegex(mapping, r"SUBGHZ_CUSTOM_BTN_LEFT:\s*return 0x7")

    def test_counter_is_advanced_once_per_upload(self) -> None:
        upload = re.search(
            r"static bool subghz_protocol_encoder_superrollo_get_upload\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(upload)
        self.assertEqual(upload.group(0).count("generic.cnt ="), 1)

    def test_upload_capacity_is_proven_before_writes(self) -> None:
        self.assertIn("_Static_assert(", self.source)
        self.assertIn(
            "SUPERROLLO_UPLOAD_SIZE <= SUPERROLLO_UPLOAD_CAPACITY", self.source
        )
        upload = re.search(
            r"static bool subghz_protocol_encoder_superrollo_get_upload\(.+?\n}\n",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(upload)
        body = upload.group(0)
        self.assertLess(
            body.index("size_upload >= SUPERROLLO_UPLOAD_SIZE"),
            body.index("encoder.upload[index++]"),
        )

    def test_protocol_is_internal_and_does_not_bump_public_api(self) -> None:
        self.assertIn("subghz_protocol_superrollo_create_data", self.header)
        self.assertNotIn(
            "subghz_protocol_superrollo_create_data",
            PUBLIC_API.read_text(encoding="utf-8"),
        )
        self.assertNotIn(
            "subghz_protocol_superrollo_create_data",
            API_SYMBOLS.read_text(encoding="utf-8"),
        )

    def test_registry_and_manual_creation_are_wired(self) -> None:
        registry = (ROOT / "lib/subghz/protocols/protocol_items.c").read_text(
            encoding="utf-8"
        )
        create = (
            ROOT
            / "applications/main/subghz/helpers/subghz_txrx_create_protocol_key.c"
        ).read_text(encoding="utf-8")
        scene = (
            ROOT / "applications/main/subghz/scenes/subghz_scene_set_type.c"
        ).read_text(encoding="utf-8")
        self.assertIn("&subghz_protocol_superrollo", registry)
        self.assertIn("subghz_protocol_superrollo_create_data", create)
        self.assertIn('"Superrollo 433MHz"', scene)


if __name__ == "__main__":
    unittest.main()
