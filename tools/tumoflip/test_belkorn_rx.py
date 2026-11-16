"""Synthetic BelKorn receive/inspect framing. No transmitter or counter generator."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_upstream_20261104 import PRELUDE, declarations, body

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "lib/subghz/protocols/belkorn.c"


class BelKornReceiveTests(unittest.TestCase):
    def source(self):
        if not PATH.exists():
            native("void subghz_protocol_decoder_belkorn_reset(void*);\n"
                   "int main(void){subghz_protocol_decoder_belkorn_reset(0);return 0;}\n")
        return PATH.read_text()

    def context(self, source):
        return (PRELUDE + "typedef struct SubGhzProtocolDecoderBelKorn SubGhzProtocolDecoderBelKorn;\n"
                + declarations(source, "belkorn") + "\n"
                + "\n".join(re.findall(r"(?m)^#define BELKORN_.*$", source)) + "\n")

    def test_complete_repeated_frames_preserve_both_words_and_metadata(self):
        source = self.source()
        actual = "\n".join(body(source, name) for name in (
            "subghz_protocol_belkorn_frame_valid", "subghz_protocol_belkorn_metadata",
            "subghz_protocol_decoder_belkorn_reset", "subghz_protocol_decoder_belkorn_feed"))
        native(self.context(source) + actual + r'''
static uint64_t tail;
static unsigned high_counter;
static void capture(SubGhzProtocolDecoderBase*b,void*c){(void)c;SubGhzProtocolDecoderBelKorn*d=(void*)b;
 assert(d->generic.data==expected&&d->generic.data_2==tail&&d->generic.data_count_bit==118);
 assert(d->generic.serial==0x12345678&&d->generic.cnt==high_counter);received++;}
static void frame(SubGhzProtocolDecoderBelKorn*d,uint64_t hi,uint64_t lo,unsigned count){
 subghz_protocol_decoder_belkorn_feed(d,false,7696);
 for(unsigned i=0;i<count;i++){
  bool bit=i<64?((hi>>(63-i))&1):i<118?((lo>>(117-i))&1):false;
  subghz_protocol_decoder_belkorn_feed(d,!(i&1),bit?962:481);
 }
 subghz_protocol_decoder_belkorn_feed(d,true,481);
 subghz_protocol_decoder_belkorn_feed(d,false,7696);
}
int main(void){
 for(unsigned parity=0;parity<2;parity++)for(unsigned button=1;button<=32;button<<=1){
  SubGhzProtocolDecoderBelKorn d={.base={.callback=capture}};
  subghz_protocol_decoder_belkorn_reset(&d);received=0;
  expected=(0x801ULL<<52)|(0xAULL<<48)|(0x12345678ULL<<16)|0xCC55;
  tail=(0x11ULL<<46)|(0x22ULL<<38)|(0x33ULL<<30)|(0x44ULL<<22)|(0x55ULL<<14)|((0xA0ULL|parity)<<6)|button;
  high_counter=((parity?0x44:0x55)<<8)|0xA0|parity;
  frame(&d,expected,tail,118);assert(!received);
  frame(&d,expected,tail,118);assert(received==1&&d.generic.btn==button);
  frame(&d,expected,tail,118);assert(received==1);
  for(unsigned count=0;count<150;count++)if(count!=118){
   subghz_protocol_decoder_belkorn_reset(&d);received=0;
   frame(&d,expected,tail,count);frame(&d,expected,tail,count);assert(!received);
  }
  for(unsigned bad=0;bad<64;bad++)if(!bad||(bad&(bad-1))){
   subghz_protocol_decoder_belkorn_reset(&d);received=0;
   frame(&d,expected,(tail&~63ULL)|bad,118);frame(&d,expected,(tail&~63ULL)|bad,118);assert(!received);
  }
 }
 FUZZ_DECODER(SubGhzProtocolDecoderBelKorn,subghz_protocol_decoder_belkorn_feed,4);
 return 0;
}
''')
        self.assertIn(".encoder = NULL", source)
        self.assertNotIn("SubGhzProtocolFlag_Send", source)
        registry = (ROOT / "lib/subghz/protocols/plugin_registry.c").read_text()
        self.assertRegex(registry, r'PACK_GROUP\(SubGhzProtocolPackGroupCore\), "belkorn"')

    def test_file_roundtrip_rejects_partial_conflicting_or_overwide_tail(self):
        source = self.source()
        actual = "\n".join(body(source, name) for name in (
            "subghz_protocol_belkorn_frame_valid", "subghz_protocol_belkorn_metadata",
            "subghz_protocol_decoder_belkorn_serialize", "subghz_protocol_decoder_belkorn_deserialize"))
        native(self.context(source) + r'''
typedef enum {SubGhzProtocolStatusOk,SubGhzProtocolStatusError,SubGhzProtocolStatusErrorParserOthers}SubGhzProtocolStatus;
typedef int SubGhzRadioPreset;
typedef struct {unsigned bits;uint64_t key;uint8_t data[8],key2[8];bool has_data,has_key2,fail_write,fail_read,fail_rewind;unsigned count;}FlipperFormat;
static SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric*g,FlipperFormat*f,SubGhzRadioPreset*p){(void)p;f->bits=g->data_count_bit;f->key=g->data;return SubGhzProtocolStatusOk;}
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric*g,FlipperFormat*f,unsigned bits){g->data=f->key;g->data_count_bit=f->bits;return bits==f->bits?SubGhzProtocolStatusOk:SubGhzProtocolStatusError;}
static bool flipper_format_rewind(FlipperFormat*f){return !f->fail_rewind;}
static bool flipper_format_write_hex(FlipperFormat*f,const char*k,const uint8_t*b,size_t n){assert(!strcmp(k,"Data")&&n==8);if(f->fail_write)return false;memcpy(f->data,b,8);f->has_data=true;f->count=8;return true;}
static bool flipper_format_get_value_count(FlipperFormat*f,const char*k,uint32_t*n){*n=f->count;return !strcmp(k,"Data")?f->has_data:f->has_key2;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*b,size_t n){if(f->fail_read||n!=8)return false;bool ok=!strcmp(k,"Data")?f->has_data:f->has_key2;if(ok)memcpy(b,!strcmp(k,"Data")?f->data:f->key2,8);return ok;}
''' + actual + r'''
int main(void){(void)received;(void)expected;(void)subghz_protocol_belkorn_const;
 SubGhzProtocolDecoderBelKorn d={.generic={.data=0x801A12345678CC55ULL,.data_2=0x123456789ABC01ULL,.data_count_bit=118}};
 FlipperFormat f={0};assert(subghz_protocol_decoder_belkorn_serialize(&d,&f,NULL)==SubGhzProtocolStatusOk&&f.has_data);
 SubGhzProtocolDecoderBelKorn restored={0};assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)==SubGhzProtocolStatusOk);
 assert(restored.generic.data==d.generic.data&&restored.generic.data_2==d.generic.data_2);
 memcpy(f.key2,f.data,8);f.has_key2=true;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)==SubGhzProtocolStatusOk);
 f.key2[0]^=1;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);f.key2[0]^=1;
 f.has_data=false;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)==SubGhzProtocolStatusOk);
 f.has_key2=false;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);
 f.has_data=true;f.count=7;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);f.count=8;
 f.data[0]|=0x80;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);f.data[0]&=0x7F;
 f.fail_read=true;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);f.fail_read=false;
 f.fail_rewind=true;assert(subghz_protocol_decoder_belkorn_deserialize(&restored,&f)!=SubGhzProtocolStatusOk);
 f.fail_write=true;assert(subghz_protocol_decoder_belkorn_serialize(&d,&f,NULL)!=SubGhzProtocolStatusOk);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
