"""Production-C receive regressions; synthetic frames only, never radio TX."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class Unleashed094DecoderTests(unittest.TestCase):
    def test_nice_long_frames_and_press_grouping(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        constants = "\n".join(re.findall(r"^#define NICE_.*$", source, re.M))
        struct = source[source.index("struct SubGhzProtocolDecoderNiceFlorS {"):]
        struct = struct[:struct.index("\n};") + 3]
        feed = function(source, "void subghz_protocol_decoder_nice_flor_s_feed(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define furi_assert(x) assert(x)
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
typedef struct Base {void (*callback)(struct Base*,void*);void* context;void* protocol;} SubGhzProtocolDecoderBase;
typedef struct {uint64_t decode_data;uint32_t te_last;uint16_t decode_count_bit;uint8_t parser_step;} SubGhzBlockDecoder;
typedef struct {uint64_t data,data_2;uint16_t data_count_bit;uint32_t serial,cnt,seed,cnt_2;uint8_t btn;} SubGhzBlockGeneric;
typedef struct SubGhzProtocolDecoderNiceFlorS SubGhzProtocolDecoderNiceFlorS;
enum {NiceFlorSDecoderStepReset,NiceFlorSDecoderStepCheckHeader,NiceFlorSDecoderStepFoundHeader,NiceFlorSDecoderStepSaveDuration,NiceFlorSDecoderStepCheckDuration};
static const struct {uint32_t te_short,te_long,te_delta,min_count_bit_for_found;} subghz_protocol_nice_flor_s_const={500,1000,300,52};
static void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* d,bool bit){d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;}
''' + constants + "\n" + struct + "\n" + feed + r'''
static unsigned received;
static SubGhzBlockGeneric frames[32];
static void capture(SubGhzProtocolDecoderBase* b,void* ctx){(void)ctx;assert(received<32);frames[received++]=((SubGhzProtocolDecoderNiceFlorS*)b)->generic;}
static void packet(SubGhzProtocolDecoderNiceFlorS* d,uint64_t key,uint64_t tail,unsigned bits){
    subghz_protocol_decoder_nice_flor_s_feed(d,false,19000);
    subghz_protocol_decoder_nice_flor_s_feed(d,true,1500);
    subghz_protocol_decoder_nice_flor_s_feed(d,false,1500);
    for(unsigned i=0;i<bits;i++){
        bool bit=i<52?((key>>(51-i))&1):((tail>>(bits-i-1))&1);
        subghz_protocol_decoder_nice_flor_s_feed(d,true,bit?1000:500);
        subghz_protocol_decoder_nice_flor_s_feed(d,false,bit?500:1000);
    }
    subghz_protocol_decoder_nice_flor_s_feed(d,true,1500);
}
int main(void){
    SubGhzProtocolDecoderNiceFlorS d={.base={.callback=capture},.ic=0xffff};
    packet(&d,0x123456789abcdULL,0x123456789abcdefULL,112);
    assert(received==1 && frames[0].data_count_bit==112);
    assert(frames[0].data==0x123456789abcdULL && frames[0].data_2==0x123456789abcdefULL);
    memset(&d,0,sizeof(d));d.base.callback=capture;d.ic=0xffff;received=0;
    const uint64_t a=0x123456789abcdULL,b=0x223456789abcdULL;
    for(unsigned i=0;i<4;i++)packet(&d,a ^ ((uint64_t)i<<44),0,52);
    assert(received==1 && frames[0].data==a);
    packet(&d,b,0,52);packet(&d,b,0,52);
    assert(received==2 && frames[1].data==b);
    // A 72-bit capture retains its entire trailing 20-bit payload.
    memset(&d,0,sizeof(d));d.base.callback=capture;d.ic=0xffff;received=0;
    packet(&d,a,0xabcde,72);packet(&d,a,0xabcde,72);
    assert(received==1 && frames[0].data_count_bit==72 && frames[0].data_2==0xabcde);
    return 0;
}
''')

    def test_came_inspection_preserves_the_original_key(self):
        source = (ROOT / "lib/subghz/protocols/came_atomo.c").read_text()
        signatures = re.findall(r"static (?:void|uint64_t) subghz_protocol_came_atomo_remote_controller\(SubGhzBlockGeneric\* instance\) \{", source)
        self.assertEqual(len(signatures), 1)
        body = function(source, signatures[0])
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct {uint64_t data,data_2;uint32_t serial,cnt,cnt_2;uint8_t btn;} SubGhzBlockGeneric;
static void atomo_decrypt(uint8_t* p){(void)p;}
static unsigned subghz_custom_btn_get_original(void){return 1;}
static void subghz_custom_btn_set_original(unsigned b){(void)b;}
static void subghz_custom_btn_set_max(unsigned b){(void)b;}
''' + body + r'''
int main(void){
    const uint64_t original=0x1fafef3ed0f7d9efULL;
    SubGhzBlockGeneric g={.data=original};
    subghz_protocol_came_atomo_remote_controller(&g);
    assert(g.data==original);uint32_t serial=g.serial,counter=g.cnt;
    subghz_protocol_came_atomo_remote_controller(&g);
    assert(g.data==original && g.serial==serial && g.cnt==counter);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
