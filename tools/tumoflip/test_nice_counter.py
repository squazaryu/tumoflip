"""Plain O-Code counters at existing cipher boundaries; no radio or key search."""
from pathlib import Path
import os
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_nice_raw_passive import nice_encoder_source, nice_counter_source

ROOT = Path(__file__).resolve().parents[2]


def counter_header():
    # A baseline source predates this private header. Do not introduce an unused
    # new helper into that reproducer: its failure must be the old counter value.
    if os.environ.get("TUMO_NICE_SOURCE"):
        return ""
    return nice_counter_source()


class NiceCounterTests(unittest.TestCase):
    def test_shared_optional_metadata_reader_preserves_legacy_and_error_semantics(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define FURI_LOG_E(...) ((void)0)
#define NICE_FLOR_S_IC_PLAIN 0xffff
typedef struct{unsigned rewinds,fail_at;bool mode,has_ic;uint32_t value;uint16_t ic;}FlipperFormat;
static uint8_t nice_flors_counter_mode;
static bool flipper_format_rewind(FlipperFormat*f){return ++f->rewinds!=f->fail_at;}
static bool flipper_format_read_uint32(FlipperFormat*f,const char*k,uint32_t*v,unsigned n){assert(!strcmp(k,"CounterMode")&&n==1);if(!f->mode)return false;*v=f->value;return true;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*v,unsigned n){assert(!strcmp(k,"IC")&&n==2);if(!f->has_ic)return false;v[0]=f->ic>>8;v[1]=f->ic;return true;}
''' + function(source, "static bool nice_flor_s_read_modes(") + r'''
int main(void){
 uint16_t ic=0;bool o=true;FlipperFormat f={0};nice_flors_counter_mode=2;
 assert(nice_flor_s_read_modes(&f,&ic,&o)&&ic==0xffff&&!o&&nice_flors_counter_mode==2&&f.rewinds==2);
 f=(FlipperFormat){.mode=true,.value=257,.has_ic=true,.ic=0x1234};assert(nice_flor_s_read_modes(&f,&ic,&o)&&ic==0x1234&&o&&nice_flors_counter_mode==1);
 f=(FlipperFormat){.has_ic=true,.ic=0xffff};assert(nice_flor_s_read_modes(&f,&ic,&o)&&ic==0xffff&&o);
 for(unsigned at=1;at<=2;at++){f=(FlipperFormat){.fail_at=at};ic=42;o=true;assert(!nice_flor_s_read_modes(&f,&ic,&o)&&ic==42&&o);}
 return 0;
}
''')

    def test_encoder_preserves_plain_variants_override_and_literal_modes(self):
        body = nice_encoder_source()
        body = body.replace("static uint8_t nice_flors_counter_mode;", "static uint8_t nice_flors_counter_mode;static int32_t mult=1;static bool overridden;static uint32_t override_count;")
        body = body.replace("(void)c;return false;", "if(!overridden)return false;*c=override_count;return true;")
        body = body.replace("furi_hal_subghz_get_rolling_counter_mult(void){return 1;}", "furi_hal_subghz_get_rolling_counter_mult(void){return mult;}")
        native(body + r'''
static uint16_t decode(uint16_t wire){uint16_t out=0x8b89;for(unsigned n=0;n<16;n++)if(wire&(1u<<n)){out^=(uint16_t)(1u<<n);if(n%4==1)out^=(uint16_t)((1u<<(n-1))|(1u<<(n+1)));}return out;}
static uint16_t oracle(uint16_t plain){return decode(plain^0x8b89)^0x8b89;}
static void check(bool o,unsigned bits,uint16_t before,unsigned mode,int32_t step,bool user,uint16_t after,uint16_t air){
 LevelDuration samples[4096];SubGhzProtocolEncoderNiceFlorS e={.generic={.serial=123,.cnt=before,.btn=1,.data_count_bit=bits},.encoder={4096,samples},.o_code=o,.ic=0x1234};
 nice_flors_counter_mode=mode;mult=step;overridden=user;override_count=123;bypass=false;subghz_block_generic_global.cnt_need_override=user;
 subghz_protocol_encoder_nice_flor_s_get_upload(&e,1,"synthetic");assert(e.generic.cnt==after);
 for(unsigned p=0;p<16;p++){unsigned at=p*(bits==72?148:108);uint64_t frame=0;for(unsigned n=0;n<52;n++)frame=(frame<<1)|(samples[at+3+n*2].duration==1000);assert((frame&0xffff)==air);}
}
int main(void){
 for(unsigned bits=52;bits<=72;bits+=20){
  const uint16_t counts[]={0,1,7,8,63,64,0x1249,0x7fff,0xfffe,0xffff};
  for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++){
   uint16_t n=counts[i],next=(uint16_t)(n+1);
   check(true,bits,n,0,1,false,next,oracle(next));
   check(false,bits,n,0,1,false,next,next);
   for(unsigned mode=0;mode<=2;mode++){
    check(true,bits,n,mode,1,true,123,oracle(123));
    check(false,bits,n,mode,1,true,123,123);
   }
  }
  check(true,bits,decode(0xfffe),1,1,false,decode(1),1);
  check(true,bits,decode(1),1,1,false,decode(0xfffe),0xfffe);
  check(true,bits,decode(0),2,1,false,decode(1),1);
  check(true,bits,decode(1),2,1,false,decode(0),0);
  check(true,bits,decode(0),0,-0x7fffffff,false,decode(1),1);
  check(true,bits,decode(1),0,-0x7fffffff,false,decode(0xfffe),0xfffe);
  check(true,bits,decode(0xfffe),0,-0x7fffffff,false,decode(0xffff),0xffff);
  check(true,bits,decode(0xffff),0,-0x7fffffff,false,decode(0),0);
  check(false,bits,0xfffe,1,1,false,1,1);
  check(false,bits,1,2,1,false,0,0);
 }
 return 0;
}
''')

    def test_actual_decoder_and_generator_use_plain_counts_and_preserve_old_keys(self):
        source = Path(os.environ.get("TUMO_NICE_SOURCE", ROOT / "lib/subghz/protocols/nice_flor_s.c")).read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE UINT64_MAX
#define NICE_FLOR_S_IC_PLAIN 0xffffu
static bool unavailable;
static void subghz_protocol_nice_o_mask(uint8_t p,uint32_t* a,uint16_t* b){(void)p;*a=0;*b=0;}
static uint8_t subghz_protocol_nice_o_get_parcel(uint64_t d){return ((((d>>48)&15)^((d>>44)&15))^15);}
static uint64_t subghz_protocol_nice_flor_s_decrypt_ic(uint64_t d,uint16_t ic,const char* f){(void)ic;(void)f;return unavailable?UINT64_MAX:d&0xfffffffffffULL;}
static uint64_t subghz_protocol_nice_flor_s_encrypt_ic(uint64_t d,uint16_t ic,const char* f){return subghz_protocol_nice_flor_s_decrypt_ic(d,ic,f);}
static uint16_t oracle(uint16_t count){
 uint16_t wire=count^0x8b89u,result=wire;
 for(unsigned shift=1;shift<16;shift+=4)if(wire&(1u<<shift))result^=(uint16_t)((1u<<(shift-1))|(1u<<(shift+1)));
 return result;
}
''' + counter_header() + "\n" + function(source, "static bool subghz_protocol_nice_o_decrypt(")
            + "\n" + function(source, "static bool subghz_protocol_nice_o_make_data(") + r'''
int main(void){
 uint16_t count=0;uint32_t serial=0;uint8_t button=0;
 // Existing saved Key bytes must be read as-is, not rewritten or double-converted.
 const uint64_t saved=0x2d012345124eULL;
 assert(subghz_protocol_nice_o_decrypt(saved,0x1234,"fixture",&serial,&count,&button));
 assert(count==0x9cc2); // Upstream-corrected interpretation; saved Key bytes unchanged.
 for(uint32_t n=0;n<=65535;n++){
  uint64_t key=0;
  assert(subghz_protocol_nice_o_make_data(0x12345,n,2,0,0x1234,"fixture",&key));
  assert((key&0xffffu)==oracle(n));
  assert(subghz_protocol_nice_o_decrypt(key,0x1234,"fixture",&serial,&count,&button));
  assert(count==n&&serial==0x12345&&button==2);
  assert(subghz_protocol_nice_o_decrypt(key,0x1234,"fixture",NULL,NULL,NULL));
 }
 uint64_t key=0;assert(!subghz_protocol_nice_o_make_data(0x10000000,1,1,0,1,"fixture",&key));
 assert(!subghz_protocol_nice_o_make_data(1,1,16,0,1,"fixture",&key));
 assert(!subghz_protocol_nice_o_make_data(1,1,1,0,0xffff,"fixture",&key));
 assert(!subghz_protocol_nice_o_make_data(1,1,1,0,1,"fixture",NULL));
 unavailable=true;
 assert(!subghz_protocol_nice_o_make_data(1,1,1,0,1,"fixture",&key));
 assert(!subghz_protocol_nice_o_decrypt(saved,1,"fixture",&serial,&count,&button));
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
