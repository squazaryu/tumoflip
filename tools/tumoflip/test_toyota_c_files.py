"""Toyota C metadata and tail bits must survive save/reopen without an encoder."""
from pathlib import Path
import unittest
from tools.tumoflip.test_toyota_vag_diagnostics import function
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class ToyotaCFilesTests(unittest.TestCase):
    def test_saved_variants_and_tail_are_checked(self):
        source = (ROOT / "lib/subghz/protocols/toyota.c").read_text()
        signatures = ["SubGhzProtocolStatus subghz_protocol_decoder_toyota_serialize(",
                      "SubGhzProtocolStatus subghz_protocol_decoder_toyota_deserialize("]
        self.assertIn(".encoder = NULL", source)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define furi_assert assert
typedef int SubGhzProtocolStatus;typedef int SubGhzRadioPreset;
enum{SubGhzProtocolStatusOk,SubGhzProtocolStatusError,SubGhzProtocolStatusErrorParserOthers,SubGhzProtocolStatusErrorParserBitCount};
typedef struct{uint64_t data,data_2;uint16_t data_count_bit;uint32_t cnt,serial,btn;}SubGhzBlockGeneric;
typedef struct{SubGhzBlockGeneric generic;uint32_t hop,serial;uint8_t button,variant;}SubGhzProtocolDecoderToyota;
typedef struct{uint64_t key;uint32_t bit,variant,tail;bool has_variant,has_tail,write_ok;}FlipperFormat;
const struct{int min_count_bit_for_found;}toyota_const_b={67};
bool flipper_format_rewind(FlipperFormat*f){(void)f;return true;}
bool flipper_format_write_uint32(FlipperFormat*f,const char*n,const uint32_t*v,unsigned count){
 assert(count==1);if(!f->write_ok)return false;
 if(!strcmp(n,"ToyotaVariant")){f->has_variant=true;f->variant=*v;}
 else {assert(!strcmp(n,"ToyotaTail"));f->has_tail=true;f->tail=*v;}return true;
}
bool flipper_format_read_uint32(FlipperFormat*f,const char*n,uint32_t*v,unsigned count){
 assert(count==1);if(!strcmp(n,"ToyotaVariant")){*v=f->variant;return f->has_variant;}
 assert(!strcmp(n,"ToyotaTail"));*v=f->tail;return f->has_tail;
}
static SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric*g,FlipperFormat*f,SubGhzRadioPreset*p){
 (void)p;f->key=g->data;f->bit=g->data_count_bit;return SubGhzProtocolStatusOk;
}
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric*g,FlipperFormat*f,unsigned min){
 g->data=f->key;g->data_count_bit=f->bit;g->cnt=0;return f->bit>=min?SubGhzProtocolStatusOk:SubGhzProtocolStatusErrorParserBitCount;
}
''' + "\n".join(function(source, sig) for sig in signatures) + r'''
int main(void){
 SubGhzProtocolDecoderToyota a={0},b={0};a.generic.data=UINT64_C(0x123456781234567B);
 for(unsigned variant=0;variant<3;variant++){
  a.variant=variant;a.generic.data_count_bit=variant==2?66:(variant==1?67:68);a.generic.data_2=3;
  FlipperFormat f={.write_ok=true};assert(subghz_protocol_decoder_toyota_serialize(&a,&f,NULL)==0);
  assert(f.has_variant&&f.variant==variant);if(variant==2)assert(f.has_tail&&f.tail==3);
  assert(subghz_protocol_decoder_toyota_deserialize(&b,&f)==0);
  assert(b.variant==variant&&b.generic.data==a.generic.data);
  if(variant==2){assert(b.generic.data_2==3);f.tail=4;assert(subghz_protocol_decoder_toyota_deserialize(&b,&f)!=0);f.tail=3;f.has_tail=false;assert(subghz_protocol_decoder_toyota_deserialize(&b,&f)!=0);}
  f.variant=7;assert(subghz_protocol_decoder_toyota_deserialize(&b,&f)!=0);
 }
 FlipperFormat legacy={.bit=67};assert(subghz_protocol_decoder_toyota_deserialize(&b,&legacy)==0&&b.variant==1);
 legacy.bit=68;assert(subghz_protocol_decoder_toyota_deserialize(&b,&legacy)==0&&b.variant==0);
 legacy.bit=66;assert(subghz_protocol_decoder_toyota_deserialize(&b,&legacy)!=0);
 FlipperFormat failure={0};assert(subghz_protocol_decoder_toyota_serialize(&a,&failure,NULL)!=0);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
