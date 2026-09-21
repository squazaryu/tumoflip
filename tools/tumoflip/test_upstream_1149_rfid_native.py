"""Run the actual RFID format code against boundary and parity cases on host."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c, function

ROOT = Path(__file__).resolve().parents[2]


def c_source(relative):
    return "\n".join(line for line in (ROOT / relative).read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class RfidManualNativeTests(unittest.TestCase):
    def test_all_formats_roundtrip_boundaries_and_reject_parity_damage(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define furi_check assert
#define furi_crash(...) abort()
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {char text[1024];} FuriString;
static void furi_string_cat_printf(FuriString* s,const char* format,...) {
    va_list ap;va_start(ap,format);size_t n=strlen(s->text);
    vsnprintf(s->text+n,sizeof(s->text)-n,format,ap);va_end(ap);
}
''' + c_source("lib/bit_lib/bit_lib.h") + c_source("lib/bit_lib/bit_lib.c")
        + c_source("applications/main/lfrfid/lfrfid_hid_format.h")
        + c_source("applications/main/lfrfid/lfrfid_hid_format.c")
        + c_source("applications/main/lfrfid/lfrfid_casi_format.h")
        + c_source("applications/main/lfrfid/lfrfid_casi_format.c") + r'''
int main(void) {
    for(size_t i=0;i<LFRFID_HID_FORMAT_COUNT;i++) {
        const LfRfidHidFormat* format=lfrfid_hid_format_get(i);
        uint64_t fmax=lfrfid_hid_format_get_facility_code_max(format);
        uint64_t cmax=lfrfid_hid_format_get_card_number_max(format);
        uint64_t fcs[]={0,fmax/2,fmax}, cards[]={0,1,cmax/2,cmax};
        for(size_t f=0;f<COUNT_OF(fcs);f++)for(size_t c=0;c<COUNT_OF(cards);c++) {
            uint8_t data[6];uint64_t fc=99,cn=99;
            lfrfid_hid_format_encode(format,fcs[f],cards[c],data);
            assert(lfrfid_hid_format_decode(format,data,&fc,&cn));
            assert(fc==fcs[f] && cn==cards[c]);
            size_t parity_position=HID_FIELD_BIT_SIZE-format->bit_size;
            bit_lib_set_bit(data,parity_position,!bit_lib_get_bit(data,parity_position));
            assert(!lfrfid_hid_format_decode(format,data,&fc,&cn));
        }
    }
    assert(lfrfid_hid_format_get(LFRFID_HID_FORMAT_COUNT)==NULL);
    uint32_t cards[]={0,1,195537,195538,LFRFID_CASI_CARD_MAX};
    for(uint32_t credential=150000;credential<=159999;credential+=9999) {
        for(size_t i=0;i<COUNT_OF(cards);i++) {
            uint8_t data[5];uint32_t c=0,n=0;
            lfrfid_casi_format_encode(credential,cards[i],data);
            assert(lfrfid_casi_format_decode(data,&c,&n));
            assert(c==credential && n==cards[i]);
            data[0]|=0x80;assert(!lfrfid_casi_format_decode(data,&c,&n));
        }
    }
    return 0;
}
''')

    def test_empty_numeric_input_means_zero_and_invalid_input_is_rejected(self):
        source = (ROOT / "applications/services/gui/modules/number_input.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
typedef struct {const char* text_buffer;} NumberInputModel;
#define StrintParseNoError 0
static bool furi_string_empty(const char* s) {return !s[0];}
static const char* furi_string_get_cstr(const char* s) {return s;}
static int strint_to_int64(const char* s,void* unused,int64_t* out,int base) {
    (void)unused;char* end;errno=0;*out=strtoll(s,&end,base);
    return errno || *end || end==s;
}
''' + function(source, "static bool number_input_get_value(") + r'''
int main(void) {
    NumberInputModel model={.text_buffer=""};int64_t value=99;
    assert(number_input_get_value(&model,&value) && value==0);
    model.text_buffer="-2147483648";assert(number_input_get_value(&model,&value) && value==INT32_MIN);
    model.text_buffer="2147483647";assert(number_input_get_value(&model,&value) && value==INT32_MAX);
    model.text_buffer="-";assert(!number_input_get_value(&model,&value));
    model.text_buffer="999999999999999999999999";assert(!number_input_get_value(&model,&value));
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
