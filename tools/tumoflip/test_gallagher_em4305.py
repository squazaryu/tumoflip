"""Gallagher EM4305 regressions against actual request/bit/protocol code.

Raw-frame tests stub encoding only to exhaustively address all 96 input bits;
full-frame tests compile the real encoder, decoder and Manchester implementation.
No storage, radio or connected tag is used. Upstream: Unleashed 5ab2996a (#1217).
"""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_lfrfid_write_fill import source
from tools.tumoflip.test_nfc_completion_equality import native as run_c

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL = ROOT / "lib/lfrfid/protocols/protocol_gallagher.c"


def support():
    body = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define furi_assert assert
#define furi_check assert
#define furi_crash(...) abort()
#define COUNT_OF(x) (sizeof(x)/sizeof((x)[0]))
typedef struct {char text[256];} FuriString;
void furi_string_printf(FuriString*s,const char*f,...){va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);}
void furi_string_cat_printf(FuriString*s,const char*f,...){va_list a;va_start(a,f);size_t n=strlen(s->text);vsnprintf(s->text+n,sizeof(s->text)-n,f,a);va_end(a);}
'''
    for path in (
        "lib/toolbox/level_duration.h", "lib/toolbox/protocols/protocol.h",
        "lib/bit_lib/bit_lib.h", "lib/bit_lib/bit_lib.c",
        "lib/toolbox/manchester_decoder.h", "lib/toolbox/manchester_decoder.c",
        "lib/lfrfid/tools/t5577.h", "lib/lfrfid/tools/em4305.h",
        "lib/lfrfid/tools/hitagmicro.h", "lib/lfrfid/tools/hitags.h",
        "lib/lfrfid/protocols/lfrfid_protocols.h",
        "lib/lfrfid/protocols/lfrfid_write_fill.h",
        "lib/lfrfid/protocols/lfrfid_write_fill.c",
    ):
        if path == "lib/toolbox/level_duration.h":
            # Flattened test translation units otherwise warn about ordinary
            # unused header inlines. Keep -Werror for all production bodies.
            body += '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wunused-function"\n'
        body += source(path) + "\n"
        if path == "lib/toolbox/level_duration.h":
            body += '#pragma GCC diagnostic pop\n'
    return body + r'''
/* Independent byte/bit oracle: no production reversal helper is used here. */
static uint32_t lsb_word(const uint8_t* bytes){
 uint32_t value=0;for(unsigned bit=0;bit<32;bit++)
  if(bytes[bit/8]&(0x80u>>(bit%8)))value|=UINT32_C(1)<<bit;
 return value;
}
'''


class GallagherEm4305Tests(unittest.TestCase):
    def test_all_96_bits_tail_independence_and_request_guards(self):
        callback = function(PROTOCOL.read_text(), "bool protocol_gallagher_write_data(")
        run_c(support() + r'''
typedef struct {uint8_t encoded_data[14];} ProtocolGallagher;
void protocol_gallagher_encoder_start(ProtocolGallagher*p){(void)p;}
void protocol_gallagher_decode(ProtocolGallagher*p){(void)p;}
''' + callback + r'''
int main(void){
 for(unsigned sample=0;sample<100;sample++)for(unsigned tail=0;tail<256;tail+=255){
  ProtocolGallagher p={0};
  if(sample<96)p.encoded_data[sample/8]=0x80u>>(sample%8);
  else for(unsigned i=0;i<12;i++)p.encoded_data[i]=sample==96?0:sample==97?0xff:sample==98?0x55:0xaa;
  p.encoded_data[12]=tail;p.encoded_data[13]=~tail;
  ProtocolGallagher original=p;
  struct {uint64_t left;LFRFIDWriteRequest value;uint64_t right;} r;
  memset(&r,0xa5,sizeof(r));r.value.write_type=LFRFIDWriteTypeEM4305;
  LFRFIDWriteRequest before=r.value;
  assert(protocol_gallagher_write_data(&p,&r.value));
  assert(r.value.em4305.word[4]==(EM4x05_MODULATION_MANCHESTER|EM4x05_SET_BITRATE(32)|(7u<<EM4x05_MAXBLOCK_SHIFT)));
  assert(r.value.em4305.mask==0xf0);
  for(unsigned word=0;word<3;word++){
   assert(r.value.em4305.word[word+5]==lsb_word(p.encoded_data+word*4));
   before.em4305.word[word+5]=r.value.em4305.word[word+5];
  }
  before.em4305.word[4]=r.value.em4305.word[4];before.em4305.mask=r.value.em4305.mask;
  assert(!memcmp(&before,&r.value,sizeof(before)));
  assert(!memcmp(&original,&p,sizeof(p)));
  assert(r.left==UINT64_C(0xa5a5a5a5a5a5a5a5)&&r.right==r.left);
  r.value.write_type=LFRFIDWriteTypeHitagS;before=r.value;
  assert(!protocol_gallagher_write_data(&p,&r.value));assert(!memcmp(&before,&r.value,sizeof(before)));
 }
 return 0;
}
''')

    def test_real_encode_write_decode_and_t5577_frame_equivalence(self):
        run_c(support() + source("lib/lfrfid/protocols/protocol_gallagher.c") + r'''
static uint32_t random_state=0x194305;
int main(void){
 for(unsigned sample=0;sample<259;sample++){
  uint8_t credential[8];
  for(unsigned i=0;i<8;i++){
   random_state=random_state*1664525u+1013904223u;
   credential[i]=sample==0?0:sample==1?0xff:sample==2?(uint8_t[]){0x4a,0x12,0x34,0x56,0x78,0x9a,0xbc,0xde}[i]:random_state>>24;
  }
  ProtocolGallagher t={0},e={0},rx={0};memcpy(t.data,credential,8);memcpy(e.data,credential,8);
  memset(e.encoded_data+12,0xff,2); /* The unused next-preamble bytes cannot change the request. */
  LFRFIDWriteRequest a={.write_type=LFRFIDWriteTypeT5577},b={.write_type=LFRFIDWriteTypeEM4305};
  assert(protocol_gallagher_write_data(&t,&a));assert(protocol_gallagher_write_data(&e,&b));
  assert(!memcmp(t.data,e.data,8));assert(a.t5577.blocks_to_write==4&&b.em4305.mask==0xf0);
  assert(a.t5577.block[0]==(LFRFID_T5577_MODULATION_MANCHESTER|LFRFID_T5577_BITRATE_RF_32|(3u<<LFRFID_T5577_MAXBLOCK_SHIFT)));
  for(unsigned word=0;word<3;word++){
   uint8_t bytes[4];for(unsigned i=0;i<4;i++)bytes[i]=a.t5577.block[word+1]>>(24-i*8);
   assert(b.em4305.word[word+5]==lsb_word(bytes));
   for(unsigned bit=0;bit<32;bit++)if(b.em4305.word[word+5]&(UINT32_C(1)<<bit))
    rx.encoded_data[(word*32+bit)/8]|=0x80u>>((word*32+bit)%8);
  }
  memcpy(rx.encoded_data+12,rx.encoded_data,2);
  assert(protocol_gallagher_can_be_decoded(&rx));protocol_gallagher_decode(&rx);
  assert(!memcmp(rx.data,t.data,8));
  memcpy(rx.encoded_data,t.encoded_data,12);memcpy(rx.encoded_data+12,t.encoded_data,2);
  bit_lib_set_bit(rx.encoded_data,95,!bit_lib_get_bit(rx.encoded_data,95));
  assert(!protocol_gallagher_can_be_decoded(&rx));
 }
 return 0;
}
''')

    def test_shared_builder_keeps_inversion_explicit(self):
        run_c(support() + r'''
int main(void){
 uint8_t bytes[12]={0x80,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0x01};
 LFRFIDWriteRequest a={0},b={0};uint32_t config=EM4x05_MODULATION_BIPHASE|EM4x05_SET_BITRATE(64)|EM4x05_SET_NUM_BLOCKS(3);
 lfrfid_write_fill_em4305(&a,config,bytes,false);lfrfid_write_fill_em4305(&b,config,bytes,true);
 assert(a.em4305.word[4]==config&&b.em4305.word[4]==config&&a.em4305.mask==0xf0&&b.em4305.mask==0xf0);
 for(unsigned i=0;i<3;i++)assert(a.em4305.word[i+5]==lsb_word(bytes+4*i)&&b.em4305.word[i+5]==~a.em4305.word[i+5]);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
