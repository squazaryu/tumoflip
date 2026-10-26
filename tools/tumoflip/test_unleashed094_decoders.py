"""Production-C receive regressions; synthetic frames only, never radio TX."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class Unleashed094DecoderTests(unittest.TestCase):
    def test_nice_held_frames_are_cleared_on_hops_and_overruns(self):
        for app in ("applications/main/subghz", "applications_user/arf_subghz_full"):
            source = (ROOT / app / "helpers/subghz_txrx.c").read_text()
            overrun = function(source, "static void subghz_txrx_worker_overrun_callback(")
            self.assertIn("subghz_txrx_clear_nice_press(", overrun)
            hopper = function(source, "void subghz_txrx_hopper_update(")
            self.assertIn("subghz_txrx_receiver_reset(instance)", hopper)

    def test_nice_unknown_code_roundtrip_and_transmit_guard(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        structs = []
        for name in ("Decoder", "Encoder"):
            start = source.index(f"struct SubGhzProtocol{name}NiceFlorS {{")
            structs.append(source[start:source.index("\n};", start) + 3])
        production = "\n".join(function(source, signature) for signature in (
            "static bool nice_flor_s_read_modes(",
            "static bool nice_flor_s_read_known(",
            "SubGhzProtocolStatus subghz_protocol_decoder_nice_flor_s_serialize(",
            "SubGhzProtocolStatus\n    subghz_protocol_decoder_nice_flor_s_deserialize(",
            "SubGhzProtocolStatus\n    subghz_protocol_encoder_nice_flor_s_deserialize(",
        ))
        fixture = (ROOT / "tools/tumoflip/fixtures/unleashed094/nice_format.c").read_text()
        native(fixture.replace("/* @STRUCTS@ */", "\n".join(structs)).replace("/* @PRODUCTION@ */", production))

    def test_press_identity_avoids_hash_collisions(self):
        common = (ROOT / "lib/subghz/protocols/common.c").read_text()
        history = (ROOT / "applications/main/subghz/subghz_history.c").read_text()
        getter = "bool subghz_protocol_decoder_get_press_identity("
        match = "static bool subghz_history_is_duplicate("
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define furi_assert(x) assert(x)
#define SUBGHZ_PRESS_IDENTITY_WORDS 5
typedef struct {int id;} SubGhzProtocol;
static const SubGhzProtocol subghz_protocol_came_atomo={1},subghz_protocol_beninca_arc={2},subghz_protocol_nice_flor_s={3},other={4};
typedef struct {const SubGhzProtocol* protocol;} SubGhzProtocolDecoderBase;
typedef struct {uint64_t data,data_2;uint32_t serial,cnt,seed;uint16_t data_count_bit;uint8_t btn;} Generic;
typedef Generic SubGhzBlockGeneric;
typedef struct {SubGhzProtocolDecoderBase base;Generic generic;} SubGhzProtocolDecoderCommon;
typedef struct {uint32_t frequency;} SubGhzRadioPreset;
typedef struct {uint32_t last_update_air_time;uint8_t code_last_hash_data;bool code_last_hash_data_set;
    bool code_last_identity_set;uint32_t code_last_identity[5];const SubGhzProtocol* code_last_protocol;uint32_t code_last_frequency;} SubGhzHistory;
bool subghz_protocol_decoder_get_press_identity(SubGhzProtocolDecoderBase*,uint32_t*);
static bool subghz_history_is_duplicate(SubGhzHistory*,SubGhzProtocolDecoderBase*,SubGhzRadioPreset*,uint8_t,uint32_t);
''' + (function(common, getter) if getter in common else "") +
               (function(history, match) if match in history else "") + r'''
int main(void){
    SubGhzHistory h={0};SubGhzRadioPreset p={.frequency=433920000};
    SubGhzProtocolDecoderCommon d={.base={.protocol=&subghz_protocol_came_atomo},.generic={.serial=1,.cnt=2,.btn=3}};
    assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,0));
    assert(subghz_history_is_duplicate(&h,&d.base,&p,0,10));
    d.generic.btn++;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,20));
    d.generic.serial++;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,30));
    d.generic.cnt++;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,40));
    p.frequency=868350000;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,50));
    d.base.protocol=&subghz_protocol_beninca_arc;d.generic.seed=0xaa55;
    assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,60));
    assert(subghz_history_is_duplicate(&h,&d.base,&p,0,70));
    d.generic.cnt^=0x80000000u;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,80));
    assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,580));
    // A missing/wrong ARC key must use complete raw data, not zero decoded fields.
    d.generic.seed=0;d.generic.data=9;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,590));
    d.generic.data_2=8;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,600));
    d.base.protocol=&subghz_protocol_nice_flor_s;d.generic.data_count_bit=112;
    assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,610));
    d.generic.data_2++;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,620));
    d.generic.data_count_bit=52;d.generic.data=0x123456789abcdULL;
    assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,630));
    d.generic.data^=1ULL<<44;assert(subghz_history_is_duplicate(&h,&d.base,&p,0,640));
    d.generic.data^=1ULL<<48;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,650));
    d.base.protocol=&other;assert(!subghz_history_is_duplicate(&h,&d.base,&p,0,660));
    assert(subghz_history_is_duplicate(&h,&d.base,&p,0,670));
    return 0;
}
''')

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
