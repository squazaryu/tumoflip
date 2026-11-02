"""Execute selected upstream safety regressions without NFC/RF hardware."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_subremote_lifecycle import function
from tools.tumoflip.test_hotplug_assets import function as definition
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
PROTOCOLS = ROOT / 'lib/subghz/protocols'


class UpstreamNovemberSafetyTests(unittest.TestCase):
    def test_compact_manchester_duration_mapping_preserves_all_symbols_and_errors(self):
        header = (ROOT / 'lib/toolbox/manchester_encoder.h').read_text().replace('#pragma once', '')
        for name in ['came_twee', 'power_smart', 'honeywell', 'marantec', 'revers_rb2', 'secplus_v2']:
            with self.subTest(protocol=name):
                text = (PROTOCOLS / (name + '.c')).read_text()
                symbol = 'subghz_protocol_encoder_' + name + '_add_duration_to_upload'
                constant = re.search(r'static const SubGhzBlockConst subghz_protocol_' + name + r'_const = \{.*?\};', text, re.S).group()
                native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>
static jmp_buf trap;
_Noreturn static void invalid_symbol(const char* msg) {
    assert(!strcmp(msg,"SubGhz: ManchesterEncoderResult is incorrect."));longjmp(trap,1);
}
#define furi_crash invalid_symbol
typedef struct {bool level;uint32_t duration;} LevelDuration;
typedef struct {uint32_t te_short,te_long,te_delta,min_count_bit_for_found;} SubGhzBlockConst;
static LevelDuration level_duration_make(bool l,uint32_t d) {return (LevelDuration){l,d};}
''' + header + '\n' + function((PROTOCOLS / 'common.c').read_text(), 'subghz_protocol_encoder_common_manchester_duration') + '\n' + constant + '\n' + function(text, symbol) + f'''
int main(void) {{
    for(unsigned i=0;i<4;i++) {{
        LevelDuration d={symbol}((ManchesterEncoderResult)i);
        assert(d.level==(i>=2));
        assert(d.duration==((i==1||i==2)?subghz_protocol_{name}_const.te_long:subghz_protocol_{name}_const.te_short));
    }}
    if(!setjmp(trap)) {{{symbol}((ManchesterEncoderResult)4);assert(false);}}
    if(!setjmp(trap)) {{{symbol}((ManchesterEncoderResult)-1);assert(false);}}
    return 0;
}}
''')

    def test_power_smart_worst_case_executes_real_manchester_encoder(self):
        text = (PROTOCOLS / 'power_smart.c').read_text()
        manchester = (ROOT / 'lib/toolbox/manchester_encoder.c').read_text()
        header = (ROOT / 'lib/toolbox/manchester_encoder.h').read_text().replace('#pragma once', '')
        actual = '\n'.join(function(manchester, name) for name in [
            'manchester_encoder_reset', 'manchester_encoder_advance', 'manchester_encoder_finish',
        ])
        actual += '\n' + definition(text, 'void* subghz_protocol_encoder_power_smart_alloc(')
        actual += '\n' + function(text, 'subghz_protocol_encoder_power_smart_add_duration_to_upload')
        actual += '\n' + function(text, 'subghz_protocol_encoder_power_smart_get_upload')
        native(r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define furi_assert assert
#define furi_check assert
#define furi_crash(...) abort()
#define UNUSED(x) ((void)x)
#define bit_read(v,b) (((v)>>(b))&1U)
typedef int SubGhzEnvironment;
typedef struct {bool level;uint32_t duration;} LevelDuration;
typedef struct {struct {uint64_t data;unsigned data_count_bit;} generic;
                struct {LevelDuration* upload;size_t size_upload;} encoder;} SubGhzProtocolEncoderPowerSmart;
static const int subghz_protocol_power_smart=0;
static const struct {unsigned te_short,te_long;} subghz_protocol_power_smart_const={225,450};
static size_t capacity;
static void* subghz_protocol_encoder_common_alloc(size_t size,const void* p,size_t repeat,size_t count) {
    (void)p;(void)repeat;SubGhzProtocolEncoderPowerSmart* x=calloc(1,size);assert(x);
    capacity=count;x->encoder.upload=calloc(count,sizeof(LevelDuration));assert(x->encoder.upload);
    x->encoder.size_upload=count;return x;
}
static LevelDuration level_duration_make(bool l,uint32_t d) {return (LevelDuration){l,d};}
static bool level_duration_get_level(LevelDuration d) {return d.level;}
''' + header + '\n' + function((PROTOCOLS / 'common.c').read_text(), 'subghz_protocol_encoder_common_manchester_duration') + '\n' + actual + r'''
int main(void) {
    SubGhzProtocolEncoderPowerSmart* x=subghz_protocol_encoder_power_smart_alloc(NULL);
    const uint64_t samples[]={0,UINT64_MAX,0x5555555555555555ULL,0xAAAAAAAAAAAAAAAAULL,
                              0xFD2137ACAADEC852ULL};
    size_t largest=0;
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
        x->generic.data=samples[i];x->generic.data_count_bit=64;x->encoder.size_upload=capacity;
        subghz_protocol_encoder_power_smart_get_upload(x);
        assert(x->encoder.size_upload<=capacity);
        if(x->encoder.size_upload>largest) largest=x->encoder.size_upload;
    }
    assert(largest==1025); // Old 1024 allocation writes past the actual heap buffer.
    free(x->encoder.upload);free(x);return 0;
}
''')

    def test_came_twee_saved_frame_is_validated_before_upload(self):
        text = (PROTOCOLS / 'came_twee.c').read_text()
        table = re.search(r'static const uint32_t came_twee_magic_numbers_xor\[15\] = \{.*?\};', text, re.S).group()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define furi_assert assert
typedef struct {uint64_t data;uint32_t serial,btn,cnt;} SubGhzBlockGeneric;
typedef struct {SubGhzBlockGeneric generic;struct {unsigned repeat,front;bool is_running;} encoder;} SubGhzProtocolEncoderCameTwee;
typedef struct {SubGhzBlockGeneric generic;} SubGhzProtocolDecoderCameTwee;
typedef struct {uint64_t data;int status;} FlipperFormat;
typedef int SubGhzProtocolStatus;
enum {SubGhzProtocolStatusError=-1,SubGhzProtocolStatusOk,SubGhzProtocolStatusErrorParserOthers};
static const struct {unsigned min_count_bit_for_found;} subghz_protocol_came_twee_const={54};
static unsigned uploads;
static int subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric* g,FlipperFormat* f,unsigned bits) {
    assert(bits==54);g->data=f->data;return f->status;
}
static bool flipper_format_read_uint32(FlipperFormat* f,const char* key,uint32_t* value,size_t n) {
    (void)f;(void)key;(void)value;(void)n;return false;
}
static void subghz_protocol_encoder_came_twee_get_upload(SubGhzProtocolEncoderCameTwee* x) {
    assert((x->generic.data&15)<15);uploads++;
}
static uint64_t subghz_protocol_blocks_reverse_key(uint64_t x,unsigned n) {
    uint64_t y=0;for(unsigned i=0;i<n;i++){y=(y<<1)|(x&1);x>>=1;}return y;
}
''' + table + '\n' + '\n'.join(function(text, name) for name in [
            'subghz_protocol_came_twee_remote_controller',
            'subghz_protocol_encoder_came_twee_deserialize',
            'subghz_protocol_decoder_came_twee_deserialize',
        ]) + r'''
int main(void) {
    SubGhzProtocolEncoderCameTwee e={0};SubGhzProtocolDecoderCameTwee d={0};
    FlipperFormat f={.data=0x3fff720000000fULL,.status=SubGhzProtocolStatusOk};
    assert(subghz_protocol_encoder_came_twee_deserialize(&e,&f)==SubGhzProtocolStatusErrorParserOthers);
    assert(!e.encoder.is_running&&!uploads);
    assert(subghz_protocol_decoder_came_twee_deserialize(&d,&f)==SubGhzProtocolStatusErrorParserOthers);
    for(unsigned i=0;i<15;i++) {
        f.data=0x3fff7200000000ULL|i;
        assert(subghz_protocol_decoder_came_twee_deserialize(&d,&f)==SubGhzProtocolStatusOk);
        assert(subghz_protocol_encoder_came_twee_deserialize(&e,&f)==SubGhzProtocolStatusOk);
        assert(e.encoder.is_running&&e.encoder.front==0);
    }
    f.status=SubGhzProtocolStatusError;
    assert(subghz_protocol_decoder_came_twee_deserialize(&d,&f)==SubGhzProtocolStatusError);
    assert(subghz_protocol_encoder_came_twee_deserialize(&e,&f)==SubGhzProtocolStatusError);
    assert(uploads==15);return 0;
}
''')

    def test_came_twee_does_not_index_parcel_f_outside_table(self):
        text=(PROTOCOLS/'came_twee.c').read_text()
        table=re.search(r'static const uint32_t came_twee_magic_numbers_xor\[15\] = \{.*?\};',text,re.S).group()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct{uint64_t data;uint32_t serial,btn,cnt;}SubGhzBlockGeneric;
static uint64_t subghz_protocol_blocks_reverse_key(uint64_t x,unsigned n){uint64_t y=0;for(unsigned i=0;i<n;i++){y=(y<<1)|(x&1);x>>=1;}return y;}
''' + table + '\n' + function(text,'subghz_protocol_came_twee_remote_controller') + r'''
int main(void){SubGhzBlockGeneric g={.data=0x3fff720000000fULL,.serial=123,.btn=4,.cnt=5};
 subghz_protocol_came_twee_remote_controller(&g);assert(g.serial==123&&g.btn==4&&g.cnt==5);
 for(unsigned i=0;i<15;i++){g.data=0x3fff7200000000ULL|i;subghz_protocol_came_twee_remote_controller(&g);assert(g.serial==((uint32_t)g.data^came_twee_magic_numbers_xor[i]));}
 return 0;}
''')

    def test_ultralight_rejects_exact_end_page_but_keeps_i2c_validation(self):
        text = (ROOT/'lib/nfc/protocols/mf_ultralight/mf_ultralight_listener.c').read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define FURI_LOG_T(...) ((void)0)
#define FURI_LOG_I(...) ((void)0)
enum{MfUltralightCommandNotProcessedNAK,MfUltralightCommandProcessed,MfUltralightListenerAccessTypeRead,MfUltralightListenerAccessTypeWrite,MfUltralightTypeMfulC=10};
typedef int MfUltralightCommand;
typedef struct{uint8_t bytes[8];}BitBuffer;
typedef struct{uint8_t data[4];}MfUltralightPage;
typedef struct{uint16_t pages_total;unsigned type;}Data;
typedef struct{Data*data;BitBuffer*tx_buffer;void*static_lock;}MfUltralightListener;
static bool i2c,valid_i2c=true;static unsigned touched,validated;
static uint8_t bit_buffer_get_byte(BitBuffer*b,unsigned i){return b->bytes[i];}
static const uint8_t*bit_buffer_get_data(BitBuffer*b){return b->bytes;}
static void bit_buffer_copy_bytes(BitBuffer*b,const uint8_t*p,size_t n){(void)b;(void)p;(void)n;}
static bool mf_ultralight_is_i2c_tag(unsigned t){(void)t;return i2c;}
static bool mf_ultralight_i2c_validate_pages(unsigned a,unsigned b,MfUltralightListener*x){(void)a;(void)b;(void)x;validated++;return valid_i2c;}
static bool mf_ultralight_listener_check_access(MfUltralightListener*x,unsigned p,unsigned a){(void)x;(void)p;(void)a;return true;}
static bool mf_ultralight_static_lock_check_page(void*x,unsigned p){(void)x;(void)p;return false;}
static bool mf_ultralight_dynamic_lock_check_page(MfUltralightListener*x,unsigned p){(void)x;(void)p;return false;}
static void mf_ultralight_listener_perform_read(MfUltralightPage*p,MfUltralightListener*x,unsigned a,unsigned n,bool c){(void)p;(void)n;(void)c;assert(i2c||a<x->data->pages_total);touched++;}
static int mf_ultralight_listener_perform_write(MfUltralightListener*x,const uint8_t*p,unsigned a,bool c){(void)p;(void)c;assert(i2c||a<x->data->pages_total);touched++;return MfUltralightCommandProcessed;}
static void mf_ultralight_listener_send_response(MfUltralightListener*x){(void)x;}
''' + function(text,'mf_ultralight_listener_read_page_handler') + '\n' + function(text,'mf_ultralight_listener_write_page_handler') + r'''
int main(void){BitBuffer b={0};Data d={.pages_total=16};MfUltralightListener x={.data=&d,.tx_buffer=&b};
 for(unsigned total=0;total<=255;total++){d.pages_total=total;for(unsigned page=0;page<=255;page++){
  b.bytes[1]=page;touched=0;int r=mf_ultralight_listener_read_page_handler(&x,&b);assert((r==MfUltralightCommandProcessed)==(page<total));
  r=mf_ultralight_listener_write_page_handler(&x,&b);assert((r==MfUltralightCommandProcessed)==(page<total));
 }}
 i2c=true;d.pages_total=4;b.bytes[1]=255;assert(mf_ultralight_listener_read_page_handler(&x,&b)==MfUltralightCommandProcessed);
 valid_i2c=false;assert(mf_ultralight_listener_write_page_handler(&x,&b)==MfUltralightCommandNotProcessedNAK);assert(validated==2);return 0;}
''')

    def test_ansonic_rejects_slow_nice_rate_without_rejecting_valid_bits(self):
        text = (PROTOCOLS/'ansonic.c').read_text()
        constants = '\n'.join(re.findall(r'^#define ANSONIC_.*$', text, re.M))
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define furi_assert assert
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
enum{AnsonicDecoderStepReset,AnsonicDecoderStepFoundStartBit,AnsonicDecoderStepSaveDuration,AnsonicDecoderStepCheckDuration};
typedef struct{uint32_t parser_step,te_last,header_count,decode_count_bit;uint64_t decode_data;}SubGhzBlockDecoder;
typedef struct{struct{void(*callback)(void*,void*);void*context;}base;SubGhzBlockDecoder decoder;struct{uint64_t data;unsigned serial,btn,data_count_bit;}generic;}SubGhzProtocolDecoderAnsonic;
static const struct{uint32_t te_short,te_long,te_delta,min_count_bit_for_found;}subghz_protocol_ansonic_const={555,1111,120,12};
static void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder*d,bool b){d->decode_data=(d->decode_data<<1)|b;d->decode_count_bit++;}
''' + constants + '\n' + function(text,'subghz_protocol_decoder_ansonic_feed') + r'''
int main(void){SubGhzProtocolDecoderAnsonic x={0};
 for(unsigned s=530;s<=580;s+=10){x.decoder=(SubGhzBlockDecoder){.parser_step=AnsonicDecoderStepCheckDuration,.te_last=s};subghz_protocol_decoder_ansonic_feed(&x,true,1666-s);assert(x.decoder.decode_count_bit==1);}
 x.decoder=(SubGhzBlockDecoder){.parser_step=AnsonicDecoderStepCheckDuration,.te_last=610};subghz_protocol_decoder_ansonic_feed(&x,true,1220);assert(x.decoder.parser_step==AnsonicDecoderStepReset&&!x.decoder.decode_count_bit);
 x.decoder=(SubGhzBlockDecoder){.parser_step=AnsonicDecoderStepCheckDuration,.te_last=1667};subghz_protocol_decoder_ansonic_feed(&x,true,UINT32_MAX);assert(!x.decoder.decode_count_bit);return 0;}
''')

    def test_encoder_allocations_cover_worst_case(self):
        cases = [('came_atomo','SubGhzProtocolEncoderCameAtomo',1024),
                 ('power_smart','SubGhzProtocolEncoderPowerSmart',1040),
                 ('somfy_keytis','SubGhzProtocolEncoderSomfyKeytis',560)]
        for name,typ,minimum in cases:
            text=(PROTOCOLS/(name+'.c')).read_text()
            native(r'''
#include <assert.h>
#include <stddef.h>
#define UNUSED(x) ((void)x)
typedef int SubGhzEnvironment;
static size_t allocated;
static void*subghz_protocol_encoder_common_alloc(size_t a,const void*b,size_t c,size_t d){(void)a;(void)b;(void)c;allocated=d;return &allocated;}
''' + f'typedef int {typ};\nstatic const int subghz_protocol_{name}=0;\n' + definition(text,'void* subghz_protocol_encoder_'+name+'_alloc(') +
                   f'\nint main(void){{subghz_protocol_encoder_{name}_alloc(NULL);assert(allocated>={minimum});return 0;}}')


if __name__ == '__main__': unittest.main()
