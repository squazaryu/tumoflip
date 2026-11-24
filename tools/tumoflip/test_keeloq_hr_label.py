"""Display-only regressions; synthetic metadata and no RF or device IO."""

from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_erreka_868_add_manually import without_includes

ROOT = Path(__file__).resolve().parents[2]


class KeeLoqDisplayTests(unittest.TestCase):
    def test_history_marks_half_rate_even_after_the_existing_manufacturer_alias(self):
        native(r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define KEELOQ_TE_FAST 200u
typedef struct { char text[80]; } FuriString;
typedef struct { uint32_t te; bool has_te,bad_rewind; const char* manufacturer; } FlipperFormat;
static const char* furi_string_get_cstr(FuriString* s){return s->text;}
static bool furi_string_empty(FuriString* s){return !s->text[0];}
static void furi_string_set_str(FuriString* s,const char* text){snprintf(s->text,sizeof(s->text),"%s",text);}
static void furi_string_set(FuriString* s,FuriString* value){*s=*value;}
static void furi_string_printf(FuriString* s,const char* fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(s->text,sizeof(s->text),fmt,ap);va_end(ap);}
static bool flipper_format_rewind(FlipperFormat* f){return !f->bad_rewind;}
static bool flipper_format_read_string(FlipperFormat* f,const char* name,FuriString* s){
 assert(!strcmp(name,"Manufacture"));if(!f->manufacturer)return false;furi_string_set_str(s,f->manufacturer);return true;
}
static bool flipper_format_read_uint32(FlipperFormat* f,const char* key,uint32_t* value,unsigned n){
 assert(n==1);if(strcmp(key,"TE"))return false;*value=f->te;return f->has_te;
}
static bool flipper_format_read_hex(FlipperFormat* f,const char* key,uint8_t* out,unsigned n){(void)f;(void)key;(void)out;(void)n;return false;}
""" + without_includes((ROOT / "lib/subghz/protocols/history_name.h").read_text()) + r"""
static void check(const char* initial,const char* expected,FlipperFormat f){
 FuriString name={0},scratch={0};furi_string_set_str(&name,initial);
 subghz_history_name_variant(&name,&f,&scratch);assert(!strcmp(name.text,expected));
}
int main(void){
 (void)furi_string_printf;
 check("KL Demo","KL HR Demo",(FlipperFormat){200,true,false,"Demo"});
 check("KeeLoq","KL HR Demo",(FlipperFormat){200,true,false,"Demo"});
 check("KL Demo","KL Demo",(FlipperFormat){400,true,false,"Demo"});
 check("KL Demo","KL Demo",(FlipperFormat){0,false,false,"Demo"});
 check("KL Demo","KL Demo",(FlipperFormat){199,true,false,"Demo"});
 check("KL Demo","KL Demo",(FlipperFormat){200,true,true,"Demo"});
 check("KL Demo","KL Demo",(FlipperFormat){200,true,false,NULL});
 check("Other","Other",(FlipperFormat){200,true,false,"Demo"});
 return 0;
}
""")

    def test_detail_heading_marks_only_the_existing_fast_timing(self):
        source = (ROOT / "lib/subghz/protocols/keeloq.c").read_text()
        code = function(source, "void subghz_protocol_decoder_keeloq_get_string(")
        normalizer = function(source, "static inline uint32_t subghz_protocol_keeloq_te_nominal(")
        native(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define furi_assert assert
#define KEELOQ_TE_FAST 200u
#define KEELOQ_DEA_MIO_DISC_MASK 0x3ffu
typedef int ProgMode;
enum { PROG_MODE_KEELOQ_BFT=1 };
typedef struct {char text[512];} FuriString;
typedef struct {uint64_t data;unsigned data_count_bit;const char* protocol_name;unsigned btn,cnt,seed;} Generic;
typedef struct {Generic generic;void* keystore;const char* manufacture_name;uint32_t te;} SubGhzProtocolDecoderKeeloq;
static struct {bool btn_is_available,cnt_is_available;unsigned current_btn,btn_length_bit,cnt_length_bit,current_cnt;} subghz_block_generic_global;
static const struct {uint32_t te_short;} subghz_protocol_keeloq_const={400u};
static uint32_t subghz_protocol_keeloq_check_remote_controller(Generic*g,void*k,const char**m){(void)g;(void)k;(void)m;return 0;}
static uint64_t subghz_protocol_blocks_reverse_key(uint64_t v,unsigned n){(void)v;(void)n;return 0;}
static ProgMode subghz_custom_btn_get_prog_mode(void){return 0;}
static void furi_string_cat_printf(FuriString*s,const char*fmt,...){
 va_list ap;va_start(ap,fmt);size_t at=strlen(s->text);vsnprintf(s->text+at,sizeof(s->text)-at,fmt,ap);va_end(ap);
}
""" + normalizer + "\n" + code + r"""
int main(void){
 (void)subghz_protocol_keeloq_te_nominal;
 const char* manufacturers[]={"AN-Motors","HCS101","BFT","Erreka","Unknown","Demo","Dea_Mio"};
 for(unsigned i=0;i<sizeof(manufacturers)/sizeof(*manufacturers);i++){
  SubGhzProtocolDecoderKeeloq instance={.generic={.protocol_name="KeeLoq",.data_count_bit=66},.manufacture_name=manufacturers[i]};
  FuriString normal={0},fast={0};
  instance.te=400;subghz_protocol_decoder_keeloq_get_string(&instance,&normal);
  instance.te=200;subghz_protocol_decoder_keeloq_get_string(&instance,&fast);
  assert(strstr(normal.text,"KeeLoq 66bit\r\n")==normal.text);
  assert(strstr(fast.text,"KeeLoq 66b HR\r\n")==fast.text);
 }
 return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
