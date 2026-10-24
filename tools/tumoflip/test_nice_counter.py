"""Plain O-Code counters at existing cipher boundaries; no radio or key search."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


def counter_header():
    header = ROOT / "lib/subghz/protocols/nice_o_counter.h"
    return header.read_text() if header.exists() else ""


class NiceCounterTests(unittest.TestCase):
    def test_actual_decoder_and_generator_use_plain_counts_and_preserve_old_keys(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
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
 const uint8_t digits[]={1,0,6,7,5,4,2,3};uint16_t result=count&0x8000u;
 for(unsigned shift=0;shift<15;shift+=3)result|=(uint16_t)digits[(count>>shift)&7]<<shift;
 return result;
}
''' + counter_header() + "\n" + function(source, "static bool subghz_protocol_nice_o_decrypt(")
            + "\n" + function(source, "static bool subghz_protocol_nice_o_make_data(") + r'''
int main(void){
 uint16_t count=0;uint32_t serial=0;uint8_t button=0;
 // Existing saved Key bytes must be read as-is, not rewritten or double-converted.
 const uint64_t saved=0x2d0123450006ULL;
 assert(subghz_protocol_nice_o_decrypt(saved,0x1234,"fixture",&serial,&count,&button));
 assert(count==2); // Raw +1 used to move this counter by the wrong number of presses.
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
