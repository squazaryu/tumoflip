"""Malformed weather packets must be ignored, never take down Weather Editor."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
WEATHER = ROOT / "applications_user/weather_editor/protocols"


def receiver():
    source = (WEATHER / "lacrosse_tx.c").read_text()
    generic = (WEATHER / "ws_generic.h").read_text()
    definitions = "\n".join(re.findall(r"(?m)^#define LACROSSE_TX_.*$", source))
    definitions += "\n" + "\n".join(re.findall(r"(?m)^#define WS_NO_.*$", generic))
    for pattern in (
        r"static const SubGhzBlockConst ws_protocol_lacrosse_tx_const = \{.*?\};",
        r"struct WSProtocolDecoderLaCrosse_TX \{.*?\n\};",
        r"typedef enum \{.*?\} LaCrosse_TXDecoderStep;",
    ):
        definitions += "\n" + re.search(pattern, source, re.S).group()
    generic_struct = re.search(r"struct WSBlockGeneric \{.*?\n\};", generic, re.S).group()
    analysis_signature = re.search(
        r"static (?:bool|void) ws_protocol_lacrosse_tx_remote_controller\(", source).group()
    production = "\n".join(function(source, signature) for signature in (
        "void ws_protocol_decoder_lacrosse_tx_reset(",
        "static bool ws_protocol_lacrosse_tx_check_crc(", analysis_signature,
        "void ws_protocol_decoder_lacrosse_tx_feed(",
    ))
    checksum = function((ROOT / "lib/subghz/blocks/math.c").read_text(),
                        "uint8_t subghz_protocol_blocks_add_bytes(")
    return r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert assert
#define furi_crash(message) do { fputs(message, stderr); abort(); } while(0)
#define DURATION_DIFF(a,b) ((a) > (b) ? (a) - (b) : (b) - (a))
typedef struct Base {void (*callback)(struct Base*, void*); void* context;} SubGhzProtocolDecoderBase;
typedef struct {unsigned parser_step, te_last; uint64_t decode_data; uint8_t decode_count_bit;} SubGhzBlockDecoder;
typedef struct {unsigned te_short, te_long, te_delta, min_count_bit_for_found;} SubGhzBlockConst;
typedef struct WSBlockGeneric WSBlockGeneric;
typedef struct WSProtocolDecoderLaCrosse_TX WSProtocolDecoderLaCrosse_TX;
void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* d, bool bit) {
    d->decode_data = (d->decode_data << 1) | bit; ++d->decode_count_bit;
}
""" + generic_struct + "\n" + definitions + "\n" + checksum + "\n" + production + r"""
static unsigned received;
static void capture(SubGhzProtocolDecoderBase* base, void* context) {
    assert(base && !context); ++received;
}
static uint64_t sample(unsigned type) {
    uint64_t data = (0xaULL << 36) | ((uint64_t)type << 32) | (0x5aULL << 24) |
                    (6ULL << 20) | (5ULL << 16) | (4ULL << 12);
    unsigned sum = 0;
    for(unsigned shift = 4; shift <= 36; shift += 4) sum += (data >> shift) & 0xf;
    return data | (sum & 0xf);
}
"""


class WeatherLaCrosseUnknownTypeTests(unittest.TestCase):
    def test_unknown_types_do_not_crash_or_mutate_decoded_fields(self):
        native(receiver() + r"""
int main(void) {
    (void)capture;
    for(unsigned type = 0; type < 16; ++type) {
        WSBlockGeneric data = {.data = sample(type), .id = 999, .temp = -123.0f,
                               .humidity = 255, .btn = 1, .battery_low = 2, .channel = 3};
        uint8_t before[sizeof(data)]; memcpy(before, &data, sizeof(data));
        ws_protocol_lacrosse_tx_remote_controller(&data);
        if(type == LACROSSE_TX_MSG_TYPE_TEMP || type == LACROSSE_TX_MSG_TYPE_HUM) {
            assert(data.id == 45 && data.btn == WS_NO_BTN && data.battery_low == WS_NO_BATT &&
                   data.channel == WS_NO_CHANNEL);
            if(type == LACROSSE_TX_MSG_TYPE_TEMP) {
                assert(data.temp > 15.39f && data.temp < 15.41f && data.humidity == WS_NO_HUMIDITY);
            } else {
                assert(data.humidity == 65 && data.temp == -123.0f);
            }
        } else {
            assert(memcmp(before, &data, sizeof(data)) == 0);
        }
    }
    return 0;
}
""")

    def test_feed_drops_unknown_types_and_bad_checksums_before_callback(self):
        native(receiver() + r"""
int main(void) {
    for(unsigned type = 0; type < 16; ++type) {
        for(unsigned corrupt = 0; corrupt < 2; ++corrupt) {
            for(unsigned callback = 0; callback < 2; ++callback) {
                WSProtocolDecoderLaCrosse_TX d = {.base = {.callback = callback ? capture : NULL}};
                ws_protocol_decoder_lacrosse_tx_reset(&d); received = 0;
                uint64_t data = sample(type) ^ corrupt;
                d.decoder.parser_step = LaCrosse_TXDecoderStepCheckDuration;
                d.decoder.decode_data = data >> 1;
                d.decoder.decode_count_bit = 43;
                d.decoder.te_last = data & 1 ? ws_protocol_lacrosse_tx_const.te_short :
                                             ws_protocol_lacrosse_tx_const.te_long;
                ws_protocol_decoder_lacrosse_tx_feed(&d, false, 4000);
                assert(received == (unsigned)(!corrupt && callback && (type == 0 || type == 14)));
                assert(d.decoder.parser_step == LaCrosse_TXDecoderStepReset &&
                       d.decoder.decode_data == 0 && d.decoder.decode_count_bit == 0 && !d.header_count);
                if(received) assert(d.generic.data == data && d.generic.data_count_bit == 44);
            }
        }
    }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
