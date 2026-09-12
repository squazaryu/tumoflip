"""Execute the production file parsers on legacy, full, and invalid captures."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class KiaFileLengthTests(unittest.TestCase):
    def test_both_parsers_preserve_supported_lengths_and_propagate_errors(self):
        for filename, name in (
            ("lib/subghz/protocols/kia_v5.c", "subghz_protocol_decoder_kia_v5_deserialize"),
            ("applications_user/protopirate/protocols/kia_v5.c", "kia_protocol_decoder_v5_deserialize"),
        ):
            with self.subTest(parser=filename):
                source = (ROOT / filename).read_text()
                start = source.index(name + "(void* context,")
                end = source.index("\n}", start) + 2
                production = "SubGhzProtocolStatus " + source[start:end]
                run_c(r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define furi_check assert
#define furi_assert assert
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-function"
#endif
typedef enum {SubGhzProtocolStatusOk, SubGhzProtocolStatusErrorParserBitCount,
              ReadError} SubGhzProtocolStatus;
typedef struct {uint16_t data_count_bit; uint64_t data; uint32_t serial,cnt; uint8_t btn;} Generic;
typedef struct {Generic generic; uint8_t crc; uint64_t yek;} SubGhzProtocolDecoderKiaV5;
typedef struct {uint16_t bits; SubGhzProtocolStatus error;} FlipperFormat;
static SubGhzProtocolStatus subghz_block_generic_deserialize(Generic* g, FlipperFormat* f) {
    if(f->error) return f->error;
    g->data_count_bit=f->bits; g->data=0x123456789abcdef0ULL; return SubGhzProtocolStatusOk;
}
static inline bool flipper_format_rewind(FlipperFormat* f) {(void)f;return true;}
static inline bool flipper_format_read_uint32(FlipperFormat* f,const char* k,uint32_t* v,uint16_t n) {
    (void)f;(void)k;(void)v;(void)n;return false;
}
static inline uint16_t mixer_decode(uint32_t value) {(void)value;return 0;}
static inline uint8_t subghz_custom_btn_get_original(void) {return 0;}
static inline void subghz_custom_btn_set_original(uint8_t value) {(void)value;}
static inline uint8_t kia_v5_btn_to_custom(uint8_t value) {return value;}
static inline void subghz_custom_btn_set_max(uint8_t value) {(void)value;}
''' + production + r'''
int main(void) {
    SubGhzProtocolDecoderKiaV5 decoder={0};
    for(unsigned bits=0;bits<=255;bits++) {
        FlipperFormat input={bits,SubGhzProtocolStatusOk};
        SubGhzProtocolStatus status=PARSER(&decoder,&input);
        assert((status==SubGhzProtocolStatusOk)==(bits==64 || bits==67));
        assert(decoder.generic.data_count_bit==bits);
        assert(decoder.generic.data==0x123456789abcdef0ULL);
    }
    FlipperFormat failed={64,ReadError};
    assert(PARSER(&decoder,&failed)==ReadError);
    return 0;
}
'''.replace("PARSER", name))


if __name__ == "__main__":
    unittest.main()
