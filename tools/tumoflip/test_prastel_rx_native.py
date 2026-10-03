"""Execute production Prastel RX code; no radio or encoder feature import."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function, run_c

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/subghz/protocols/prastel.c"


class PrastelRxNativeTests(unittest.TestCase):
    def test_serial_uses_all_21_documented_frame_bits(self):
        source = SOURCE.read_text()
        functions = "\n".join(function(source, name) for name in (
            "static uint8_t subghz_protocol_prastel_reverse_byte(",
            "static uint8_t subghz_protocol_prastel_parity(",
            "static void subghz_protocol_prastel_unscramble(",
            "static void subghz_protocol_prastel_scramble(",
            "static void subghz_protocol_prastel_unpack(",
            "static uint64_t subghz_protocol_prastel_pack(",
            "static void subghz_protocol_prastel_remote_controller(",
        ))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint64_t data;uint32_t serial;uint8_t btn;uint16_t cnt;} SubGhzBlockGeneric;
static uint8_t subghz_custom_btn_get_original(void) {return 1;}
static void subghz_custom_btn_set_original(uint8_t v) {(void)v;}
static void subghz_custom_btn_set_max(uint8_t v) {(void)v;}
''' + functions + r'''
int main(void) {
    const uint32_t serials[]={0,1,0x100,0x800,0x1000,0x8000,0x10000,0x80000,
                              0x100000,0x1fffff,0xba92d,0x1ba92d};
    for(unsigned i=0;i<sizeof(serials)/sizeof(serials[0]);i++) {
        uint32_t n=serials[i];
        /* Independent payload layout, not the production serial mapper. */
        uint8_t p[7]={0,(n>>8)&0xf0,0x70|((n>>8)&15)|((n>>13)&0x80),
                       n&255,((n>>12)&0xf0)|8,0x2a,0x13};
        subghz_protocol_prastel_scramble(p);
        SubGhzBlockGeneric g={.data=subghz_protocol_prastel_pack(p)};
        subghz_protocol_prastel_remote_controller(&g);
        assert(g.serial==n);
    }
    return 0;
}
''')

    def test_first_frame_gap_and_noise_boundaries(self):
        feed = function(SOURCE.read_text(), "void subghz_protocol_decoder_prastel_feed(")
        run_c(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define furi_assert assert
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
enum {PrastelDecoderStepReset,PrastelDecoderStepFoundStartBit,
      PrastelDecoderStepSaveDuration,PrastelDecoderStepCheckDuration};
typedef struct {uint64_t decode_data;uint32_t te_last;uint8_t parser_step,decode_count_bit;} Decoder;
typedef struct {void(*callback)(void*,void*);void* context;} Base;
typedef struct {Base base;Decoder decoder;struct {uint64_t data;uint8_t data_count_bit;} generic;} SubGhzProtocolDecoderPrastel;
static const struct {uint32_t te_short,te_long,te_delta,min_count_bit_for_found;}
subghz_protocol_prastel_const={320,640,150,42};
static void subghz_protocol_blocks_add_bit(Decoder* d,int bit) {
    d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;
}
static unsigned frames;
static void received(void* base,void* ctx) {(void)base;(void)ctx;frames++;}
''' + feed + r'''
int main(void) {
    const unsigned gaps[]={4800,24000};
    for(unsigned i=0;i<2;i++) {
        SubGhzProtocolDecoderPrastel d={.base={.callback=received}};
        subghz_protocol_decoder_prastel_feed(&d,false,gaps[i]);
        assert(d.decoder.parser_step==PrastelDecoderStepFoundStartBit);
        subghz_protocol_decoder_prastel_feed(&d,true,320);
        uint64_t key=0x123456789ab;
        for(int bit=41;bit>=0;bit--) {
            bool one=(key>>bit)&1;
            subghz_protocol_decoder_prastel_feed(&d,false,one?640:320);
            subghz_protocol_decoder_prastel_feed(&d,true,one?320:640);
        }
        subghz_protocol_decoder_prastel_feed(&d,false,24000);
        assert(d.generic.data==key && d.generic.data_count_bit==42);
    }
    assert(frames==2);
    const unsigned rejected[]={0,320,3840,27520,40000};
    for(unsigned i=0;i<5;i++) {
        SubGhzProtocolDecoderPrastel d={0};
        subghz_protocol_decoder_prastel_feed(&d,false,rejected[i]);
        assert(d.decoder.parser_step==PrastelDecoderStepReset);
    }
    SubGhzProtocolDecoderPrastel d={0};
    subghz_protocol_decoder_prastel_feed(&d,true,4800);
    assert(d.decoder.parser_step==PrastelDecoderStepReset);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
