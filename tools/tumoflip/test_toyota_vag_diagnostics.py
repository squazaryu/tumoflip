"""Host execution of receive-only Toyota framing and VAG diagnostic extraction."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class ToyotaVagTests(unittest.TestCase):
    def test_toyota_positive_frames_and_foreign_truncated_input(self):
        source = (ROOT / "lib/subghz/protocols/toyota.c").read_text()
        declarations = source[source.index("#define TAG"):source.index(
            "const SubGhzProtocolDecoder subghz_protocol_toyota_decoder")]
        signatures = [
            "static inline bool te_is_short(", "static inline bool te_is_long(",
            "static void toyota_push_bit(", "static uint32_t toyota_extract(",
            "static void toyota_decode_and_fire(",
            "void subghz_protocol_decoder_toyota_reset(",
            "static void toyota_feed_variant_a(", "static void toyota_feed_variant_b(",
            "void subghz_protocol_decoder_toyota_feed(",
        ]
        run_c(r"""
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define furi_assert assert
#define FURI_LOG_D(...) ((void)0)
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
typedef struct { uint32_t te_short,te_long,te_delta;uint16_t min_count_bit_for_found; } SubGhzBlockConst;
typedef struct Base { void (*callback)(struct Base*,void*);void* context; } SubGhzProtocolDecoderBase;
typedef struct { int parser_step;uint32_t te_last;uint64_t decode_data;uint16_t decode_count_bit; } SubGhzBlockDecoder;
typedef struct { uint64_t data;uint16_t data_count_bit;uint32_t serial,btn,cnt; } SubGhzBlockGeneric;
""" + declarations + "\n".join(function(source, s) for s in signatures) + r"""
static int found;
static uint16_t bits;
static uint32_t variant;
static void receive(SubGhzProtocolDecoderBase* b,void* c) {
 (void)c;SubGhzProtocolDecoderToyota* s=(void*)b;
 found++;bits=s->generic.data_count_bit;variant=s->generic.cnt;
}
static void feed(SubGhzProtocolDecoderToyota* s,bool l,uint32_t d) {
 subghz_protocol_decoder_toyota_feed(s,l,d);
}
static void preamble(SubGhzProtocolDecoderToyota* s,int v) {
 for(int i=0;i<12;i++){feed(s,true,v?200:400);feed(s,false,v?390:400);}
 if(v){feed(s,true,200);feed(s,false,1938);}
}
static void payload(SubGhzProtocolDecoderToyota* s,int v,int n) {
 for(int i=0;i<n;i++){
  if(v)feed(s,(i%2)==0,(i%3)?200:390);
  else {feed(s,true,(i%3)?400:800);feed(s,false,(i%3)?800:400);}
 }
 feed(s,false,10000);
}
int main(void){
 SubGhzProtocolDecoderToyota s={0};s.base.callback=receive;
 for(int v=0;v<2;v++){
  subghz_protocol_decoder_toyota_reset(&s);found=0;
  preamble(&s,v);payload(&s,v,v?67:68);
  assert(found==1 && bits==(v?67:68) && variant==(unsigned)v);
  // Reopen/repeated reception on the same decoder.
  preamble(&s,v);payload(&s,v,v?67:68);assert(found==2);
  subghz_protocol_decoder_toyota_reset(&s);found=0;
  preamble(&s,v);payload(&s,v,60);assert(found==0);
 }
 // A KIA-like sync gap after a matching preamble must not enter Toyota data.
 subghz_protocol_decoder_toyota_reset(&s);found=0;preamble(&s,0);
 feed(&s,true,400);feed(&s,false,1200);payload(&s,0,68);assert(found==0);
 // Arbitrary sub-sync noise cannot be treated as NRZ bit 1.
 subghz_protocol_decoder_toyota_reset(&s);found=0;preamble(&s,1);
 feed(&s,true,900);payload(&s,1,66);assert(found==0);
 // A bad stream does not poison a later valid variant.
 preamble(&s,1);payload(&s,1,67);assert(found==1);
 return 0;
}
""")

    def test_vag_flags_preserve_existing_button_conventions(self):
        for path in ["lib/subghz/protocols/vag.c",
                     "applications_user/protopirate/protocols/vag.c"]:
            with self.subTest(path=path):
                source = (ROOT / path).read_text()
                body = function(source, "static void vag_fill_from_decrypted(")
                expected = (
                    "((i>>4)==1 || (i>>4)==2 || (i>>4)==4 || (i>>4)==8) ? (i&0xF0) : i"
                    if path.startswith("lib/") else "(i>>4)"
                )
                run_c(r"""
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef struct { uint32_t serial,cnt;uint8_t btn,btn_flags,check_byte;bool decrypted; } SubGhzProtocolDecoderVAG;
""" + body + """
int main(void){
 for(int i=0;i<256;i++){
  SubGhzProtocolDecoderVAG s={0};
  uint8_t dec[8]={1,2,3,4,5,6,7,(uint8_t)i};
  vag_fill_from_decrypted(&s,dec,0xC0);
  assert(s.btn_flags==(i&15));
  assert(s.btn==(""" + expected + """));
  assert(s.cnt==0x070605 && s.check_byte==0xC0 && s.decrypted);
 }
 return 0;
}
""")
                save = function(source, "SubGhzProtocolStatus subghz_protocol_decoder_vag_serialize(")
                self.assertIn('if(!flipper_format_write_uint32(flipper_format, "BtnFlags"', save)
                reset = function(source, "void subghz_protocol_decoder_vag_reset(")
                self.assertIn("instance->btn_flags = 0;", reset)
        storage = (ROOT / "applications_user/protopirate/helpers/protopirate_storage.c").read_text()
        self.assertIn('protopirate_storage_copy_u32_optional(save_file, flipper_format, "BtnFlags")', storage)


if __name__ == "__main__":
    unittest.main()
