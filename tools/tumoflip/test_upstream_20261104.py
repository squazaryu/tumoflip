"""Actual decoder/encoder bodies on synthetic samples; never opens a radio."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
PROTOCOLS = ROOT / "lib/subghz/protocols"


def body(source, name):
    match = re.search(r"(?m)^(?:static\s+)?(?:void\*?|bool|uint8_t|SubGhzProtocolStatus)\s+" +
                      re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    if not match:
        raise AssertionError(f"No function definition: {name}")
    return source[match.start():source.index("\n}", match.end()) + 2]


def declarations(source, stem):
    sections = []
    for pattern in (
        rf"static const SubGhzBlockConst subghz_protocol_{stem}_const = \{{.*?\}};",
        r"struct SubGhzProtocol(?:Decoder|Encoder)\w+ \{.*?\n\};",
        r"typedef enum \{.*?\} \w+DecoderStep;",
    ):
        sections.extend(re.findall(pattern, source, re.S))
    lines = source.splitlines()
    for index, line in enumerate(lines):
        if re.match(r"#define (?:MARANTEC24|CAME_TWEE|FREEZER)_", line):
            macro = [line]
            while macro[-1].rstrip().endswith("\\"):
                index += 1
                macro.append(lines[index])
            sections.append("\n".join(macro))
    return "\n".join(sections) + "\n"


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert assert
#define furi_check assert
#define UNUSED(x) ((void)(x))
#define FURI_LOG_E(...) ((void)0)
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define bit_read(v,n) (((v)>>(n))&1U)
typedef int SubGhzEnvironment;
typedef int SubGhzProtocolEncoderBase;
typedef struct Base {void(*callback)(struct Base*,void*);void* context;} SubGhzProtocolDecoderBase;
typedef struct {uint32_t parser_step,te_last;uint64_t decode_data;uint8_t decode_count_bit;} SubGhzBlockDecoder;
typedef struct {uint64_t data,data_2;uint32_t serial,btn,cnt;uint16_t data_count_bit;} SubGhzBlockGeneric;
typedef struct {uint32_t te_short,te_long,te_delta,min_count_bit_for_found;} SubGhzBlockConst;
typedef struct {bool level;uint32_t duration;} LevelDuration;
typedef struct {LevelDuration* upload;size_t size_upload;unsigned repeat;} SubGhzProtocolBlockEncoder;
typedef struct {SubGhzProtocolDecoderBase base;SubGhzBlockDecoder decoder;SubGhzBlockGeneric generic;} SubGhzProtocolDecoderCommon;
LevelDuration level_duration_make(bool level,uint32_t duration){return (LevelDuration){level,duration};}
void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* d,uint8_t bit){d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;}
static unsigned received;
static uint64_t expected;
#define FUZZ_DECODER(Type,feed,states) do { \
    const uint32_t durations[]={0,120,250,320,500,640,800,1000,1600,2000,2400,3200,9000,15200,UINT32_MAX}; \
    for(unsigned state=0;state<(states);state++)for(unsigned level=0;level<2;level++) \
    for(unsigned a=0;a<COUNT_OF(durations);a++)for(unsigned b=0;b<COUNT_OF(durations);b++){ \
        Type noise={0};noise.decoder.parser_step=state;noise.decoder.te_last=durations[b]; \
        feed(&noise,level,durations[a]); \
    } \
}while(0)
'''


class NovemberProtocolTests(unittest.TestCase):
    def test_marantec_encoder_gap_decodes_and_keeps_captured_tolerance(self):
        source = (PROTOCOLS / "marantec24.c").read_text()
        production = "\n".join(body(source, name) for name in (
            "subghz_protocol_marantec24_check_remote_controller",
            "subghz_protocol_encoder_marantec24_get_upload",
            "subghz_protocol_decoder_marantec24_feed"))
        native(PRELUDE + "typedef struct SubGhzProtocolDecoderMarantec24 SubGhzProtocolDecoderMarantec24;\n"
               "typedef struct SubGhzProtocolEncoderMarantec24 SubGhzProtocolEncoderMarantec24;\n" +
               declarations(source, "marantec24") + production + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;assert(((SubGhzProtocolDecoderMarantec24*)b)->generic.data==expected);received++;
}
int main(void){
    LevelDuration upload[256];
    SubGhzProtocolEncoderMarantec24 e={.encoder={.upload=upload,.size_upload=256}};
    const uint64_t keys[]={0xAC05C8,0xAC05C4,0xAC05CC,0xAC05C0,0x123457};
    const uint32_t gaps[]={15200,15118,15188,14401,15999};
    for(unsigned k=0;k<COUNT_OF(keys);k++)for(unsigned g=0;g<COUNT_OF(gaps);g++){
        expected=keys[k];received=0;e.generic.data=expected;e.generic.data_count_bit=24;
        assert(subghz_protocol_encoder_marantec24_get_upload(&e));assert(e.encoder.size_upload==48);
        SubGhzProtocolDecoderMarantec24 d={.base={.callback=capture}};
        subghz_protocol_decoder_marantec24_feed(&d,false,gaps[g]);
        for(unsigned i=0;i<48;i++)subghz_protocol_decoder_marantec24_feed(&d,upload[i].level,i==47?gaps[g]:upload[i].duration);
        assert(received==1);
    }
    for(unsigned gap=0;gap<30000;gap+=1000){
        SubGhzProtocolDecoderMarantec24 d={0};
        subghz_protocol_decoder_marantec24_feed(&d,false,gap);
        if(gap<=14400||gap>=16000)assert(d.decoder.parser_step==Marantec24DecoderStepReset);
    }
    FUZZ_DECODER(SubGhzProtocolDecoderMarantec24,subghz_protocol_decoder_marantec24_feed,3);
    return 0;
}
''')

    def test_linear_repeats_match_the_actual_transmit_middleware(self):
        source = (PROTOCOLS / "linear_delta3.c").read_text()
        production = "\n".join(body(source, name) for name in (
            "subghz_protocol_encoder_linear_delta3_alloc",
            "subghz_protocol_encoder_linear_delta3_get_upload",
            "subghz_protocol_decoder_linear_delta3_reset",
            "subghz_protocol_decoder_linear_delta3_feed"))
        native(PRELUDE + r'''
typedef struct SubGhzProtocolDecoderLinearDelta3 SubGhzProtocolDecoderLinearDelta3;
typedef struct SubGhzProtocolEncoderLinearDelta3 SubGhzProtocolEncoderLinearDelta3;
''' + declarations(source, "linear_delta3") + r'''
static const int subghz_protocol_linear_delta3=0;
void* subghz_protocol_encoder_common_alloc(size_t size,const void* p,unsigned repeat,size_t count){
    (void)p;SubGhzProtocolEncoderLinearDelta3* e=calloc(1,size);assert(e);
    e->encoder.upload=calloc(count,sizeof(LevelDuration));assert(e->encoder.upload);
    e->encoder.size_upload=count;e->encoder.repeat=repeat;return e;
}
''' + production + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;assert(((SubGhzProtocolDecoderLinearDelta3*)b)->generic.data==expected);received++;
}
int main(void){
    for(unsigned key=1;key<256;key++){
        SubGhzProtocolEncoderLinearDelta3* e=subghz_protocol_encoder_linear_delta3_alloc(NULL);
        e->generic.data=key;e->generic.data_count_bit=8;
        assert(subghz_protocol_encoder_linear_delta3_get_upload(e));
        SubGhzProtocolDecoderLinearDelta3 d={.base={.callback=capture}};
        subghz_protocol_decoder_linear_delta3_reset(&d);received=0;expected=key;
        bool have=false,level=false;uint32_t duration=0;
        for(unsigned r=0;r<e->encoder.repeat;r++)for(unsigned i=0;i<e->encoder.size_upload;i++){
            LevelDuration next=e->encoder.upload[i];
            if(have&&next.level!=level){
                subghz_protocol_decoder_linear_delta3_feed(&d,level,duration);duration=0;
            }
            have=true;level=next.level;duration+=next.duration;
        }
        // Do not flush the final pending low: actual TX middleware drops it.
        assert(received>=1);
        assert(e->encoder.repeat==4);
        assert(d.decoder.decode_count_bit<=8);
        free(e->encoder.upload);free(e);
    }
    FUZZ_DECODER(SubGhzProtocolDecoderLinearDelta3,subghz_protocol_decoder_linear_delta3_feed,3);
    return 0;
}
''')

    def test_holtek_reset_cannot_confirm_a_key_from_before_reset(self):
        source = (PROTOCOLS / "holtek_ht12x.c").read_text()
        reset = re.search(r"\.reset = (\w+),", source).group(1)
        reset_source = (PROTOCOLS / "common.c").read_text() if reset.endswith("common_reset") else source
        native(PRELUDE + "typedef struct SubGhzProtocolDecoderHoltek_HT12X SubGhzProtocolDecoderHoltek_HT12X;\n"
               "typedef struct SubGhzProtocolEncoderHoltek_HT12X SubGhzProtocolEncoderHoltek_HT12X;\n" +
               declarations(source, "holtek_th12x") + body(reset_source, reset) + "\n" +
               body(source, "subghz_protocol_decoder_holtek_th12x_feed") + f"\n#define RESET {reset}\n" + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;assert(((SubGhzProtocolDecoderHoltek_HT12X*)b)->generic.data==expected);received++;
}
static void frame(SubGhzProtocolDecoderHoltek_HT12X* d,unsigned key){
    subghz_protocol_decoder_holtek_th12x_feed(d,true,320);
    for(int i=11;i>=0;i--){
        bool bit=(key>>i)&1;
        subghz_protocol_decoder_holtek_th12x_feed(d,false,bit?640:320);
        subghz_protocol_decoder_holtek_th12x_feed(d,true,bit?320:640);
    }
    subghz_protocol_decoder_holtek_th12x_feed(d,false,8960);
}
int main(void){
    SubGhzProtocolDecoderHoltek_HT12X d={.base={.callback=capture},.last_data=0xabc};
    RESET(&d);assert(d.last_data==0);received=0;expected=0xabc;
    subghz_protocol_decoder_holtek_th12x_feed(&d,false,8960);
    frame(&d,expected);assert(received==0);
    frame(&d,expected);assert(received==1);
    FUZZ_DECODER(SubGhzProtocolDecoderHoltek_HT12X,subghz_protocol_decoder_holtek_th12x_feed,4);
    return 0;
}
''')

    def test_came_twee_requires_prefix_and_preserves_parcel_bounds(self):
        source = (PROTOCOLS / "came_twee.c").read_text()
        table = re.search(r"static const uint32_t came_twee_magic_numbers_xor\[15\] = \{.*?\};", source, re.S).group()
        native(PRELUDE + r'''
typedef int ManchesterState;
typedef int ManchesterEvent;
enum {ManchesterEventReset,ManchesterEventShortLow,ManchesterEventLongLow,ManchesterEventShortHigh,ManchesterEventLongHigh};
static bool provide_bit;
bool manchester_advance(int old,int event,int* next,bool* bit){
    (void)old;(void)next;if(bit&&provide_bit){*bit=event&1;return true;}return false;
}
typedef struct SubGhzProtocolDecoderCameTwee SubGhzProtocolDecoderCameTwee;
typedef struct SubGhzProtocolEncoderCameTwee SubGhzProtocolEncoderCameTwee;
''' + declarations(source, "came_twee") + table + "\n" + body(source, "subghz_protocol_decoder_came_twee_feed") + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;assert(((SubGhzProtocolDecoderCameTwee*)b)->generic.data==expected);received++;
}
int main(void){
    for(unsigned parcel=0;parcel<16;parcel++)for(unsigned prefix=0;prefix<3;prefix++){
        const uint64_t prefixes[]={0x3fff72ULL,0x3fff73ULL,0ULL};
        assert(came_twee_magic_numbers_xor[parcel%15]!=0);
        expected=(prefixes[prefix]<<32)|0x12345670|parcel;received=0;
        SubGhzProtocolDecoderCameTwee d={.base={.callback=capture}};
        d.decoder.parser_step=CameTweeDecoderStepDecoderData;
        d.decoder.decode_data=expected;d.decoder.decode_count_bit=54;
        subghz_protocol_decoder_came_twee_feed(&d,false,3000);
        assert(received==(unsigned)(prefix==0&&parcel<15));
    }
    for(unsigned mode=0;mode<2;mode++){
        provide_bit=mode;
        FUZZ_DECODER(SubGhzProtocolDecoderCameTwee,subghz_protocol_decoder_came_twee_feed,2);
    }
    return 0;
}
''')

    def test_freezer_is_external_and_bounded(self):
        path = PROTOCOLS / "freezer.c"
        if not path.exists():
            # Intentional link-time RED for the new production entry point.
            native("void subghz_protocol_decoder_freezer_reset(void*);\n"
                   "int main(void){subghz_protocol_decoder_freezer_reset(0);return 0;}\n")
            return
        source = path.read_text()
        raw = (ROOT / "tools/tumoflip/fixtures/upstream_20261104/freezer_raw.sub").read_text()
        captured = [int(value) for line in raw.splitlines() if line.startswith("RAW_Data:")
                    for value in line.partition(":")[2].split()]
        production = "\n".join(body(source, name) for name in (
            "subghz_protocol_freezer_check_remote_controller",
            "subghz_protocol_encoder_freezer_get_upload",
            "subghz_protocol_decoder_freezer_reset",
            "subghz_protocol_freezer_is_short", "subghz_protocol_freezer_is_long",
            "subghz_protocol_decoder_freezer_feed"))
        native(PRELUDE + "typedef struct SubGhzProtocolDecoderFreezer SubGhzProtocolDecoderFreezer;\n"
               "typedef struct SubGhzProtocolEncoderFreezer SubGhzProtocolEncoderFreezer;\n" +
               declarations(source, "freezer") + production +
               "\nstatic const int32_t captured[]={" + ",".join(map(str, captured)) + "};\n" + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;assert(((SubGhzProtocolDecoderFreezer*)b)->generic.data==expected);received++;
}
static void frame(SubGhzProtocolDecoderFreezer* d,LevelDuration* upload,unsigned size){
    for(unsigned i=0;i<size;i++)subghz_protocol_decoder_freezer_feed(d,upload[i].level,upload[i].duration);
}
int main(void){
    const uint32_t keys[]={1,0xffffff,0x123456,0xaaaaaa,0x555555};
    for(unsigned key=0;key<COUNT_OF(keys);key++){
        LevelDuration upload[64];SubGhzProtocolEncoderFreezer e={.encoder={.upload=upload,.size_upload=64}};
        expected=keys[key];received=0;e.generic.data=expected;e.generic.data_count_bit=24;
        assert(subghz_protocol_encoder_freezer_get_upload(&e));assert(e.encoder.size_upload==50);
        SubGhzProtocolDecoderFreezer d={.base={.callback=capture}};
        subghz_protocol_decoder_freezer_reset(&d);
        subghz_protocol_decoder_freezer_feed(&d,false,9000);
        frame(&d,upload,50);assert(received==0);frame(&d,upload,50);assert(received==1);
        subghz_protocol_decoder_freezer_reset(&d);
        subghz_protocol_decoder_freezer_feed(&d,false,9000);
        frame(&d,upload,50);assert(received==1);
        // An overlong frame must not wrap its uint8_t counter into a valid24-bit one.
        subghz_protocol_decoder_freezer_feed(&d,true,250);
        for(unsigned i=0;i<280;i++){
            subghz_protocol_decoder_freezer_feed(&d,false,250);
            subghz_protocol_decoder_freezer_feed(&d,true,500);
        }
        subghz_protocol_decoder_freezer_feed(&d,false,9000);assert(received==1);
        e.generic.data_count_bit=64;e.encoder.size_upload=64;
        assert(!subghz_protocol_encoder_freezer_get_upload(&e));
        e.generic.data_count_bit=24;e.encoder.size_upload=2;
        assert(!subghz_protocol_encoder_freezer_get_upload(&e));
    }
    expected=0xc31c1f;received=0;SubGhzProtocolDecoderFreezer recording={.base={.callback=capture}};
    subghz_protocol_decoder_freezer_reset(&recording);
    for(unsigned i=0;i<COUNT_OF(captured);i++){
        int32_t pulse=captured[i];
        subghz_protocol_decoder_freezer_feed(&recording,pulse>0,pulse>0?(uint32_t)pulse:(uint32_t)-pulse);
    }
    assert(received>0);
    FUZZ_DECODER(SubGhzProtocolDecoderFreezer,subghz_protocol_decoder_freezer_feed,4);
    return 0;
}
''')
        self.assertNotIn("&subghz_protocol_freezer", (PROTOCOLS / "protocol_items.c").read_text())
        registry = (PROTOCOLS / "plugin_registry.c").read_text()
        self.assertRegex(registry, r'PACK_GROUP\(SubGhzProtocolPackGroupCore\), "freezer"')
        self.assertIn('appid="protocol_freezer"', (ROOT / "applications_user/subghz_protocols/application.fam").read_text())
        self.assertNotIn("subghz_protocol_freezer,", (ROOT / "targets/f7/api_symbols.csv").read_text())


if __name__ == "__main__":
    unittest.main()
