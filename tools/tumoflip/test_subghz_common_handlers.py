#!/usr/bin/env python3
"""Structural regression coverage for shared Sub-GHz protocol handlers."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_DIR = REPO_ROOT / "lib/subghz/protocols"
COMMON_SOURCE = PROTOCOL_DIR / "common.c"

ENCODER_HANDLERS = (
    "subghz_protocol_encoder_common_free",
    "subghz_protocol_encoder_common_stop",
    "subghz_protocol_encoder_common_yield",
)
DECODER_PREFIX_HANDLERS = (
    "subghz_protocol_decoder_common_reset",
    "subghz_protocol_decoder_common_get_hash_data",
)
DECODER_GENERIC_HANDLERS = ("subghz_protocol_decoder_common_serialize",)
COMMON_HANDLERS = (
    *ENCODER_HANDLERS,
    "subghz_protocol_decoder_common_free",
    *DECODER_PREFIX_HANDLERS,
    *DECODER_GENERIC_HANDLERS,
)


def protocol_sources_using(symbols: tuple[str, ...]) -> list[Path]:
    return [
        source
        for source in sorted(PROTOCOL_DIR.glob("*.c"))
        if source != COMMON_SOURCE
        and any(symbol in source.read_text(encoding="utf-8") for symbol in symbols)
    ]


class SubGhzCommonHandlersTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.common = COMMON_SOURCE.read_text(encoding="utf-8")

    def test_each_shared_handler_has_one_implementation_and_consumers(self) -> None:
        all_protocol_sources = "\n".join(
            source.read_text(encoding="utf-8")
            for source in sorted(PROTOCOL_DIR.glob("*.c"))
        )

        for handler in COMMON_HANDLERS:
            self.assertEqual(self.common.count(f"{handler}("), 1, handler)
            self.assertGreater(all_protocol_sources.count(handler), 1, handler)

    def test_encoder_consumers_keep_the_required_leading_layout(self) -> None:
        required_prefix = re.compile(
            r"struct\s+SubGhzProtocolEncoder\w+\s*\{\s*"
            r"SubGhzProtocolEncoderBase\s+base;\s*"
            r"SubGhzProtocolBlockEncoder\s+encoder;",
            flags=re.DOTALL,
        )

        for source in protocol_sources_using(ENCODER_HANDLERS):
            contents = source.read_text(encoding="utf-8")
            self.assertRegex(contents, required_prefix, source.name)

    def test_decoder_consumers_keep_the_required_leading_layout(self) -> None:
        decoder_prefix = re.compile(
            r"struct\s+SubGhzProtocolDecoder\w+\s*\{\s*"
            r"SubGhzProtocolDecoderBase\s+base;\s*"
            r"SubGhzBlockDecoder\s+decoder;",
            flags=re.DOTALL,
        )
        generic_prefix = re.compile(
            r"struct\s+SubGhzProtocolDecoder\w+\s*\{\s*"
            r"SubGhzProtocolDecoderBase\s+base;\s*"
            r"SubGhzBlockDecoder\s+decoder;\s*"
            r"SubGhzBlockGeneric\s+generic;",
            flags=re.DOTALL,
        )

        for source in protocol_sources_using(DECODER_PREFIX_HANDLERS):
            self.assertRegex(
                source.read_text(encoding="utf-8"), decoder_prefix, source.name
            )
        for source in protocol_sources_using(DECODER_GENERIC_HANDLERS):
            self.assertRegex(
                source.read_text(encoding="utf-8"), generic_prefix, source.name
            )

    def test_shared_protocol_sources_are_not_embedded_in_external_plugins(self) -> None:
        manifests = "\n".join(
            manifest.read_text(encoding="utf-8")
            for manifest in sorted(
                (REPO_ROOT / "applications_user").rglob("application.fam")
            )
        )

        for source in protocol_sources_using(COMMON_HANDLERS):
            relative = source.relative_to(REPO_ROOT).as_posix()
            self.assertNotIn(relative, manifests, relative)

    def test_common_implementations_preserve_protocol_semantics(self) -> None:
        for required in (
            "free(instance->encoder.upload);",
            "instance->encoder.is_running = false;",
            "instance->encoder.repeat == 0 || !instance->encoder.is_running",
            "subghz_block_generic_global.endless_tx",
            "instance->decoder.parser_step = 0;",
            "(instance->decoder.decode_count_bit / 8) + 1",
            "subghz_block_generic_serialize(&instance->generic, flipper_format, preset)",
        ):
            self.assertIn(required, self.common)


if __name__ == "__main__":
    unittest.main()
