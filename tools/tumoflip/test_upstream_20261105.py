"""Real HSM/KeeLoq timing bodies on synthetic data; no RF or key generation."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_upstream_20261104 import PRELUDE, declarations

ROOT = Path(__file__).resolve().parents[2]
PROTOCOLS = ROOT / "lib/subghz/protocols"


def production_function(source, name):
    signature = re.search(r"(?m)^(?:static\s+)?(?:inline\s+)?"
                          r"(?:void\*?|bool|uint8_t|uint32_t|size_t|SubGhzProtocolStatus)\s+" +
                          re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    if not signature:
        raise AssertionError(f"Missing production function: {name}")
    return function(source, signature.group()[:-1].rstrip())


def timing_helpers(source, selected):
    names = re.findall(r"(?m)^static (?:inline )?(?:bool|uint32_t) "
                       r"(subghz_protocol_keeloq_te_\w+)\(", source)
    return "\n".join(production_function(source, name) for name in names
                     if name.rsplit("_", 1)[-1] in selected)


def keeloq_declarations(source, helpers):
    macros = "\n".join(re.findall(r"(?m)^#define KEELOQ_TE_.*$", source))
    return (PRELUDE.replace("uint32_t serial,btn,cnt;", "uint32_t serial,btn,cnt,seed;")
            .replace("unsigned repeat;", "unsigned repeat,front;bool is_running;") + r'''
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
typedef struct {const char* text;} FuriString;
typedef struct {const char* mfname;unsigned kl_type;} SubGhzKeystore;
typedef struct SubGhzProtocolDecoderKeeloq SubGhzProtocolDecoderKeeloq;
typedef struct SubGhzProtocolEncoderKeeloq SubGhzProtocolEncoderKeeloq;
''' + macros + "\n" + declarations(source, "keeloq") + timing_helpers(source, helpers))


KEELOQ_FRAMES = r'''
static void frame_with_gap(SubGhzProtocolDecoderKeeloq* d,uint64_t data,uint32_t te,
    unsigned bits,uint32_t gap){
    for(unsigned i=0;i<11;i++){
        subghz_protocol_decoder_keeloq_feed(d,true,te);
        subghz_protocol_decoder_keeloq_feed(d,false,te);
    }
    subghz_protocol_decoder_keeloq_feed(d,true,te);
    subghz_protocol_decoder_keeloq_feed(d,false,gap);
    for(unsigned i=0;i<bits;i++){
        bool bit=i<64?((data>>(63-i))&1):true;
        subghz_protocol_decoder_keeloq_feed(d,true,bit?te:te*2);
        subghz_protocol_decoder_keeloq_feed(d,false,bit?te*2:te);
    }
    subghz_protocol_decoder_keeloq_feed(d,true,te);
    subghz_protocol_decoder_keeloq_feed(d,false,te*40);
}
static void frame(SubGhzProtocolDecoderKeeloq* d,uint64_t data,uint32_t te,unsigned bits){
    frame_with_gap(d,data,te,bits,te*10);
}
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;SubGhzProtocolDecoderKeeloq* d=(void*)b;
    assert(d->generic.data==expected && d->generic.data_count_bit==64);received++;
}
'''


FORMAT_MOCKS = r'''
typedef int SubGhzRadioPreset;
typedef enum {SubGhzProtocolStatusError=-1,SubGhzProtocolStatusOk,
    SubGhzProtocolStatusErrorEncoderGetUpload=1,SubGhzProtocolStatusErrorParserOthers,
    SubGhzProtocolStatusErrorParserKey} SubGhzProtocolStatus;
typedef struct {uint32_t input_te,saved_te,mode;unsigned te_writes,cursor;
    unsigned rewinds,fail_rewind_at;const char* manufacture;
    bool has_te,fail_te_read,fail_mode_read,fail_te_write,fail_rewind,fail_serialize,
        fail_manufacture_update;} FlipperFormat;
static uint8_t keeloq_counter_mode;
bool flipper_format_rewind(FlipperFormat* f){
    f->cursor=0;f->rewinds++;
    return !f->fail_rewind && (!f->fail_rewind_at || f->rewinds!=f->fail_rewind_at);
}
bool flipper_format_read_uint32(FlipperFormat* f,const char* name,uint32_t* value,unsigned n){
    assert(n==1);
    if(strcmp(name,"TE")==0){assert(f->cursor==0);f->cursor=1;
        if(f->has_te){*value=f->input_te;return !f->fail_te_read;}return false;}
    if(strcmp(name,"CounterMode")==0){assert(f->cursor==0);f->cursor=1;*value=f->mode;return !f->fail_mode_read;}
    return false;
}
bool flipper_format_write_uint32(FlipperFormat* f,const char* name,const uint32_t* value,unsigned n){
    assert(strcmp(name,"TE")==0 && n==1);f->te_writes++;f->saved_te=*value;
    return !f->fail_te_write;
}
bool flipper_format_read_hex(FlipperFormat* f,const char* n,uint8_t* p,size_t s){
    (void)f;(void)n;(void)p;(void)s;return false;
}
bool flipper_format_write_hex(FlipperFormat* f,const char* n,const uint8_t* p,size_t s){
    (void)f;(void)n;(void)p;(void)s;return true;
}
bool flipper_format_update_hex(FlipperFormat* f,const char* n,const uint8_t* p,size_t s){
    (void)f;(void)n;(void)p;(void)s;return true;
}
bool flipper_format_read_string(FlipperFormat* f,const char* n,FuriString* s){
    (void)n;f->cursor=1;s->text=f->manufacture?f->manufacture:"AN-Motors";return true;
}
const char* furi_string_get_cstr(FuriString* s){return s->text;}
bool flipper_format_update_string_cstr(FlipperFormat* f,const char* n,const char* s){
    (void)n;(void)s;return !f->fail_manufacture_update;
}
bool flipper_format_write_string_cstr(FlipperFormat* f,const char* n,const char* s){
    (void)f;assert(strcmp(n,"Manufacture")==0 && s);return true;
}
SubGhzProtocolStatus subghz_block_generic_deserialize(SubGhzBlockGeneric* g,FlipperFormat* f){
    (void)f;g->data_count_bit=64;return SubGhzProtocolStatusOk;
}
SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(
    SubGhzBlockGeneric* g,FlipperFormat* f,unsigned count){
    assert(count==64);return subghz_block_generic_deserialize(g,f);
}
SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric* g,
    FlipperFormat* f,SubGhzRadioPreset* p){
    (void)g;(void)p;return f->fail_serialize?SubGhzProtocolStatusError:SubGhzProtocolStatusOk;
}
uint32_t subghz_protocol_keeloq_check_remote_controller(SubGhzBlockGeneric* g,
    SubGhzKeystore* k,const char** n){(void)g;(void)k;*n="AN-Motors";return 0;}
// Do not generate/encrypt codes in a timing regression. Hold synthetic data fixed.
static bool generation_ok=true;
bool subghz_protocol_keeloq_gen_data(SubGhzProtocolEncoderKeeloq* e,uint8_t b,bool c,bool skip){
    (void)e;(void)b;(void)c;(void)skip;return generation_ok;
}
bool subghz_protocol_keeloq_receive_only(const char* n,uint32_t seed){
    (void)n;(void)seed;return false;
}
bool subghz_protocol_encoder_keeloq_get_upload(SubGhzProtocolEncoderKeeloq* e,uint8_t b){
    (void)e;(void)b;return true;
}
'''


class NovemberTimingTests(unittest.TestCase):
    def test_hormann_seven_or_eight_ones_with_exact_44_bit_framing(self):
        source = (PROTOCOLS / "hormann.c").read_text()
        production = "\n".join(production_function(source, name) for name in (
            "subghz_protocol_decoder_hormann_check_pattern",
            "subghz_protocol_decoder_hormann_feed"))
        mask = re.search(r"(?m)^#define HORMANN_HSM_PATTERN .*$", source).group()
        native(PRELUDE + r'''
typedef struct SubGhzProtocolDecoderHormann SubGhzProtocolDecoderHormann;
typedef struct SubGhzProtocolEncoderHormann SubGhzProtocolEncoderHormann;
''' + mask + "\n" + declarations(source, "hormann") + production + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;SubGhzProtocolDecoderHormann* d=(void*)b;
    assert(d->generic.data==expected && d->generic.data_count_bit==44);received++;
}
static void frame(SubGhzProtocolDecoderHormann* d,uint64_t data,unsigned bits){
    subghz_protocol_decoder_hormann_feed(d,true,12000);
    subghz_protocol_decoder_hormann_feed(d,false,500);
    for(unsigned i=0;i<bits;i++){
        bool bit=i<44?((data>>(43-i))&1):true;
        subghz_protocol_decoder_hormann_feed(d,true,bit?1000:500);
        subghz_protocol_decoder_hormann_feed(d,false,bit?500:1000);
    }
    subghz_protocol_decoder_hormann_feed(d,true,12000);
}
int main(void){
    const uint64_t frames[]={0xff123456783ULL,0xfe123456783ULL,0xfc123456783ULL,
        0xfe123456782ULL,0xfe123456781ULL,0x7e123456783ULL,0};
    for(unsigned i=0;i<COUNT_OF(frames);i++){
        SubGhzProtocolDecoderHormann d={.base={.callback=capture}};
        received=0;expected=frames[i];frame(&d,expected,44);assert(received==(unsigned)(i<2));
    }
    for(unsigned bits=0;bits<350;bits++){
        SubGhzProtocolDecoderHormann d={.base={.callback=capture}};
        received=0;expected=0xfe123456783ULL;frame(&d,expected,bits);
        assert(received==(unsigned)(bits==44));
    }
    FUZZ_DECODER(SubGhzProtocolDecoderHormann,subghz_protocol_decoder_hormann_feed,6);
    return 0;
}
''')

    def test_keeloq_both_rates_drift_reset_and_bounded_frames(self):
        source = (PROTOCOLS / "keeloq.c").read_text()
        native(keeloq_declarations(source, ("delta", "preamble")) + function(
            source, "void subghz_protocol_decoder_keeloq_reset(") + "\n" + function(
            source, "void subghz_protocol_decoder_keeloq_feed(") + KEELOQ_FRAMES + r'''
int main(void){
    const uint32_t rates[]={400,200,156,175,220,244};
    const uint64_t data[]={0x0123456789abcdefULL,0x8000000000000001ULL,UINT64_MAX};
    SubGhzKeystore store={0};
    SubGhzProtocolDecoderKeeloq d={.base={.callback=capture},.keystore=&store};
    for(unsigned i=0;i<COUNT_OF(rates);i++)for(unsigned j=0;j<COUNT_OF(data);j++){
        subghz_protocol_decoder_keeloq_reset(&d);received=0;expected=data[j];
        frame(&d,expected,rates[i],66);assert(received==1);
        frame(&d,expected,rates[i],66);assert(received==1); // unchanged duplicate filter
    }
    for(unsigned bits=0;bits<350;bits++){
        subghz_protocol_decoder_keeloq_reset(&d);received=0;expected=data[0];
        frame(&d,expected,200,bits);assert(received==(unsigned)(bits>=64 && bits<=66));
    }
    // Retain the legacy standard sync tolerance, including the overlap with fast TE.
    for(unsigned gap=2201;gap<5800;gap+=17){
        subghz_protocol_decoder_keeloq_reset(&d);received=0;expected=data[0];
        frame_with_gap(&d,expected,400,64,gap);assert(received==1);
    }
    const unsigned invalid_gaps[]={0,1000,1550,2200,5800,UINT32_MAX};
    for(unsigned i=0;i<COUNT_OF(invalid_gaps);i++){
        subghz_protocol_decoder_keeloq_reset(&d);received=0;
        frame_with_gap(&d,expected,400,64,invalid_gaps[i]);assert(received==0);
    }
    // Alternating half/normal rate frames must lock independently without a user reset.
    subghz_protocol_decoder_keeloq_reset(&d);received=0;
    for(unsigned i=0;i<20;i++){
        expected=data[i%COUNT_OF(data)];frame(&d,expected,(i&1)?400:200,64);
        assert(received==i+1);
    }
    // Neither noise nor a preamble without a sync gap may report a frame.
    subghz_protocol_decoder_keeloq_reset(&d);received=0;
    for(unsigned i=0;i<1000;i++){
        subghz_protocol_decoder_keeloq_feed(&d,true,200);
        subghz_protocol_decoder_keeloq_feed(&d,false,200);
    }
    assert(received==0);
    FUZZ_DECODER(SubGhzProtocolDecoderKeeloq,subghz_protocol_decoder_keeloq_feed,4);
    return 0;
}
''')

    def test_keeloq_timing_survives_file_round_trip_and_resets_on_legacy_load(self):
        source = (PROTOCOLS / "keeloq.c").read_text()
        production = "\n".join(production_function(source, name) for name in (
            "subghz_protocol_keeloq_read_metadata",
            "subghz_protocol_encoder_keeloq_encode_to_timings",
            "subghz_protocol_encoder_keeloq_deserialize",
            "subghz_protocol_decoder_keeloq_deserialize",
            "subghz_protocol_decoder_keeloq_serialize"))
        native(keeloq_declarations(source, ("sanitize", "nominal")) + FORMAT_MOCKS + production + r'''
int main(void){
    (void)received;(void)expected;
    SubGhzKeystore store={0};FuriString manufacturer={"AN-Motors"};
    SubGhzProtocolDecoderKeeloq d={.keystore=&store,.manufacture_from_file=&manufacturer};
    LevelDuration upload[159];
    SubGhzProtocolEncoderKeeloq e={.keystore=&store,.manufacture_from_file=&manufacturer,
        .encoder={.upload=upload,.size_upload=156},.manufacture_name="AN-Motors"};
    const uint32_t values[]={200,400,0,UINT32_MAX,199,201};
    for(unsigned i=0;i<COUNT_OF(values)+1;i++){
        FlipperFormat input={.has_te=i<COUNT_OF(values),.input_te=i<COUNT_OF(values)?values[i]:0,
            .mode=3};
        assert(subghz_protocol_decoder_keeloq_deserialize(&d,&input)==SubGhzProtocolStatusOk);
        assert(keeloq_counter_mode==3);
        FlipperFormat output={0};
        assert(subghz_protocol_decoder_keeloq_serialize(&d,&output,NULL)==SubGhzProtocolStatusOk);
        unsigned te=(i==0)?200:400;
        assert(output.te_writes==(unsigned)(te==200));
        if(te==200)assert(output.saved_te==200);
        assert(subghz_protocol_encoder_keeloq_deserialize(&e,&input)==SubGhzProtocolStatusOk);
        e.generic.data=0x123456789abcdef0ULL;e.generic.data_count_bit=64;
        assert(subghz_protocol_encoder_keeloq_encode_to_timings(&e,0,false,0)==156);
        assert(upload[0].duration==te && upload[23].duration==te*10);
        assert(upload[155].duration==te*40);
        e.manufacture_name="Sommer";
        assert(subghz_protocol_encoder_keeloq_encode_to_timings(&e,0,false,0)==156);
        assert(upload[155].duration==te*29);e.manufacture_name="AN-Motors";
        for(unsigned bit=0;bit<64;bit++){
            bool one=(e.generic.data>>(63-bit))&1;
            assert(upload[24+bit*2].level && !upload[25+bit*2].level);
            assert(upload[24+bit*2].duration==(one?te:te*2));
            assert(upload[25+bit*2].duration==(one?te*2:te));
        }
        // Appended frames must not rescale or overwrite earlier upload entries.
        for(unsigned prefix=0;prefix<3;prefix++)upload[prefix]=(LevelDuration){false,12345};
        assert(subghz_protocol_encoder_keeloq_encode_to_timings(&e,0,false,3)==159);
        for(unsigned prefix=0;prefix<3;prefix++)assert(upload[prefix].duration==12345);
        assert(upload[3].duration==te && upload[158].duration==te*40);
    }
    // Failed write is an error, not a successful half-rate capture without its TE.
    FlipperFormat input={.has_te=true,.input_te=200};
    assert(subghz_protocol_decoder_keeloq_deserialize(&d,&input)==SubGhzProtocolStatusOk);
    FlipperFormat bad={.fail_te_write=true};
    assert(subghz_protocol_decoder_keeloq_serialize(&d,&bad,NULL)==SubGhzProtocolStatusError);
    assert(bad.te_writes==1);
    bad=(FlipperFormat){.fail_serialize=true};
    assert(subghz_protocol_decoder_keeloq_serialize(&d,&bad,NULL)==SubGhzProtocolStatusError);
    assert(bad.te_writes==0);
    // A parser can modify its output before returning false. Ignore that output.
    input=(FlipperFormat){.has_te=true,.input_te=200,.fail_te_read=true,
        .mode=7,.fail_mode_read=true};
    assert(subghz_protocol_decoder_keeloq_deserialize(&d,&input)==SubGhzProtocolStatusOk);
    assert(keeloq_counter_mode==0);
    FlipperFormat legacy={0};
    assert(subghz_protocol_decoder_keeloq_serialize(&d,&legacy,NULL)==SubGhzProtocolStatusOk);
    assert(legacy.te_writes==0);
    for(unsigned step=1;step<=4;step++){
        bad=(FlipperFormat){.has_te=true,.input_te=200,.fail_rewind_at=step};
        assert(subghz_protocol_decoder_keeloq_deserialize(&d,&bad)!=SubGhzProtocolStatusOk);
        bad.rewinds=0;
        assert(subghz_protocol_encoder_keeloq_deserialize(&e,&bad)!=SubGhzProtocolStatusOk);
    }
    input=(FlipperFormat){.manufacture="Sommer(fsk476)"};
    assert(subghz_protocol_decoder_keeloq_deserialize(&d,&input)==SubGhzProtocolStatusOk);
    assert(strcmp(d.manufacture_name,"Sommer")==0 && strcmp(store.mfname,"Sommer")==0);
    bad=(FlipperFormat){.manufacture="Sommer(fsk476)",.fail_manufacture_update=true};
    assert(subghz_protocol_decoder_keeloq_deserialize(&d,&bad)!=SubGhzProtocolStatusOk);
    bad=(FlipperFormat){.fail_rewind=true};
    assert(subghz_protocol_decoder_keeloq_deserialize(&d,&bad)!=SubGhzProtocolStatusOk);
    generation_ok=false;upload[0].duration=12345;
    assert(subghz_protocol_encoder_keeloq_encode_to_timings(&e,0,false,0)==0);
    assert(upload[0].duration==12345);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
