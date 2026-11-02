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
