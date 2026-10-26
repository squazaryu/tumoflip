"""The real generic serializer writes Key only; long codecs must retain Key2."""
from pathlib import Path
import os
import re
import subprocess
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/field_rx_protocols"


class ArfLongPayloadTests(unittest.TestCase):
    def test_long_serializers_preserve_second_word_and_report_partial_write(self):
        cases = (("mercedes_v0", "mercedes_v0_serialize", "MercedesV0Decoder", 112),
                 ("mercedes_v1", "subghz_protocol_decoder_mercedes_v1_serialize", "SubGhzProtocolDecoderMercedesV1", 122),
                 ("bmw", "subghz_protocol_decoder_bmw_serialize", "SubGhzProtocolDecoderBMW", 128))
        for name, method, kind, bits in cases:
            with self.subTest(protocol=name):
                helper = APP / "rx_format.h"
                baseline = os.environ.get("TUMO_ARF_LONG_BASELINE")
                shared = re.sub(r"^#(?:include|pragma).*\n", "\n", helper.read_text(), flags=re.M) if helper.exists() and not baseline else ""
                code = subprocess.check_output(["git", "show", "701a7f5aa9:applications_user/field_rx_protocols/" + name + ".c"], cwd=ROOT, text=True) if baseline else (APP / (name + ".c")).read_text()
                body = "SubGhzProtocolStatus " + function(code, method + "(")
                read_checks = "SubGhzBlockGeneric restored={0};assert(field_rx_read_tail(&restored,&f)&&restored.data_2==d.generic.data_2);" if shared else ""
                native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define furi_check assert
#define furi_assert assert
typedef struct{uint64_t data,data_2;unsigned data_count_bit;}SubGhzBlockGeneric;
typedef enum{SubGhzProtocolStatusOk,SubGhzProtocolStatusErrorParserKey}SubGhzProtocolStatus;
typedef int SubGhzRadioPreset;
typedef struct{bool has_tail,fail;uint8_t tail[8];}FlipperFormat;
bool flipper_format_rewind(FlipperFormat*f){return f!=NULL;}
bool flipper_format_get_value_count(FlipperFormat*f,const char*k,uint32_t*n){assert(!strcmp(k,"Key2"));*n=f->has_tail?8:0;return f->has_tail;}
bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*b,size_t n){assert(!strcmp(k,"Key2")&&n==8);if(!f->has_tail||f->fail)return false;memcpy(b,f->tail,8);return true;}
bool flipper_format_insert_or_update_hex(FlipperFormat*f,const char*k,const uint8_t*b,size_t n){assert(!strcmp(k,"Key2")&&n==8);if(f->fail)return false;memcpy(f->tail,b,8);f->has_tail=true;return true;}
static SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric*g,FlipperFormat*f,SubGhzRadioPreset*p){(void)g;(void)f;(void)p;return 0;}
''' + shared + "\ntypedef struct{SubGhzBlockGeneric generic;}" + kind + ";\n" + body + f'''
int main(void){{{kind} d={{.generic={{.data=0x8123456789abcdefULL,.data_2=0x123456789abc0000ULL,.data_count_bit={bits}}}}};FlipperFormat f={{0}};
 assert({method}(&d,&f,NULL)==SubGhzProtocolStatusOk&&f.has_tail);
 const uint8_t expected[8]={{0x12,0x34,0x56,0x78,0x9a,0xbc,0,0}};assert(!memcmp(f.tail,expected,8));
 {read_checks}
 f.fail=true;assert({method}(&d,&f,NULL)!=SubGhzProtocolStatusOk);
 return 0;}}
''')


if __name__ == "__main__":
    unittest.main()
