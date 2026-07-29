#!/usr/bin/env python3
"""Source contracts for the small upstream safety fixes tracked by issue #238."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class CoreSafetyFixesTest(unittest.TestCase):
    def test_infrared_decoder_uses_element_bound_and_initializes_count(self) -> None:
        decoder = source(
            "lib/infrared/encoder_decoder/common/infrared_common_decoder.c"
        )
        self.assertIn(
            "furi_check(decoder->timings_cnt <= COUNT_OF(decoder->timings));",
            decoder,
        )
        self.assertNotIn(
            "furi_check(decoder->timings_cnt <= sizeof(decoder->timings));",
            decoder,
        )
        allocation = decoder[decoder.index("void* infrared_common_decoder_alloc") :]
        self.assertLess(
            allocation.index("decoder->timings_cnt = 0;"),
            allocation.index("return decoder;"),
        )

    def test_felica_reads_response_only_after_success(self) -> None:
        poller = source("lib/nfc/protocols/felica/felica_poller.c")
        handler = poller[poller.index("felica_poller_state_handler_list_system") :]
        success = handler.index("if(error == FelicaErrorNone)")
        response_read = handler.index(
            "instance->systems_total = response_system_code->system_count;"
        )
        self.assertLess(success, response_read)

    def test_pac_stanley_card_id_is_terminated(self) -> None:
        protocol = source("lib/lfrfid/protocols/protocol_pac_stanley.c")
        self.assertIn("uint8_t asciiCardId[9] = {0};", protocol)

    def test_pyramid_trailing_parity_uses_bit_offset(self) -> None:
        protocol = source("lib/lfrfid/protocols/protocol_pyramid.c")
        self.assertIn(
            "protocol_pyramid_get_parity("
            "const uint8_t* bits, size_t position, uint8_t type, int length)",
            protocol,
        )
        self.assertIn("bit_lib_get_bit(bits, position + length - 1)", protocol)
        self.assertIn(
            "protocol_pyramid_get_parity(source, length / 2, 1 /* odd */, length / 2)",
            protocol,
        )
        self.assertNotIn("source + length / 2", protocol)

    def test_js_timer_rejects_non_positive_interval_before_conversion(self) -> None:
        event_loop = source(
            "applications/system/js_app/modules/js_event_loop/js_event_loop.c"
        )
        validation = event_loop.index("if(interval <= 0)")
        conversion = event_loop.index("furi_ms_to_ticks((uint32_t)interval)")
        self.assertLess(validation, conversion)
        self.assertIn("MJS_BAD_ARGS_ERROR", event_loop[validation:conversion])

    def test_single_byte_bit_read_does_not_touch_next_byte(self) -> None:
        bit_lib = source("lib/bit_lib/bit_lib.c")
        one_byte = bit_lib.index("else if(shift + length <= 8)")
        cross_byte = bit_lib.index("data[position / 8 + 1]", one_byte)
        self.assertIn(
            "return (uint8_t)(data[position / 8] << shift) >> (8 - length);",
            bit_lib[one_byte:cross_byte],
        )


if __name__ == "__main__":
    unittest.main()
