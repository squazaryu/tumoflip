"""Native receive/configuration regressions; no radio or device filesystem.

Compile the actual Feron decoder and Standard/ARF settings loaders. Only the
FlipperFormat/storage transport is mocked. PRs Unleashed #1219/#1220 are open;
these tests define our bounded adaptation rather than importing their branches.
"""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_lfrfid_write_fill import source
from tools.tumoflip.test_nfc_completion_equality import native as run_c
from tools.tumoflip.test_upstream_20261104 import PRELUDE, declarations

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = {
    "standard": "applications/main/subghz",
    "arf": "applications_user/arf_subghz_full",
}


def feron_support():
    text = (ROOT / "lib/subghz/protocols/feron.c").read_text()
    # Use the real bit accumulator, not a copy of its implementation in PRELUDE.
    prelude = PRELUDE.replace(
        "void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* d,uint8_t bit){"
        "d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;}",
        function((ROOT / "lib/subghz/blocks/decoder.c").read_text(),
                 "void subghz_protocol_blocks_add_bit("))
    return (prelude + "typedef struct SubGhzProtocolDecoderFeron SubGhzProtocolDecoderFeron;\n"
            + declarations(text, "feron")
            + function(text, "void subghz_protocol_decoder_feron_feed(") + r'''
static void capture(SubGhzProtocolDecoderBase* base,void* context){
 (void)context;SubGhzProtocolDecoderFeron* d=(SubGhzProtocolDecoderFeron*)base;
 assert(d->generic.data==expected);assert(d->generic.data_count_bit==32);received++;
}
/* Independent pulse generator: never calls the encoder being verified. */
static unsigned __attribute__((unused)) decode(uint32_t data,unsigned bits,bool callback){
 SubGhzProtocolDecoderFeron d={0};if(callback)d.base.callback=capture;
 expected=data;received=0;subghz_protocol_decoder_feron_feed(&d,false,4500);
 for(unsigned bit=bits;bit;bit--){
  bool one=bit<=32&&((data>>(bit-1))&1u);
  subghz_protocol_decoder_feron_feed(&d,true,one?750:350);
  subghz_protocol_decoder_feron_feed(&d,false,bit==1?500:one?350:750);
 }
 subghz_protocol_decoder_feron_feed(&d,true,500);
 subghz_protocol_decoder_feron_feed(&d,false,4500);
 assert(d.decoder.decode_count_bit<=32);return received;
}
''')


def enum_source(path, name):
    text = (ROOT / path).read_text()
    match = re.search(r"typedef enum \{[^}]*\} " + re.escape(name) + ";", text)
    if not match:
        raise AssertionError(f"Missing production enum {name}")
    return match.group(0) + "\n"


SETTINGS_PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define UNUSED(x) ((void)(x))
#define furi_assert assert
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
#define EXT_PATH(x) "/ext/" x
#define RECORD_STORAGE "storage"
#define FSE_OK 0
#define SUBGHZ_RAW_THRESHOLD_MIN (-90.0f)
typedef struct {int unused;} Storage;
typedef struct {int unused;} FlipperFormat;
typedef struct {char text[2048];} FuriString;
static Storage storage;static FlipperFormat format;
static unsigned strings,formats,records,rewinds;
static bool sd_ok=true,open_ok=true,header_ok=true,partial_failure;
static uint32_t version=3;
static const char* header_type="Flipper SubGhz Last Setting File";
typedef struct {const char* name;uint32_t value;} Entry;
static Entry entries[]={
 {"Frequency",433920000},{"Preset",1},{"RawFrequency",315000000},{"RawPreset",4},
 {"FeedbackLevel",2},{"HoppingMode",3},{"ProtocolPackGroup",2},
 {"IgnoreFilter",0},{"Filter",1026},{"TXPower",5},
};
static bool ignore_missing,fields_missing;
void* furi_record_open(const char*n){assert(!strcmp(n,RECORD_STORAGE));records++;return &storage;}
void furi_record_close(const char*n){assert(!strcmp(n,RECORD_STORAGE)&&records);records--;}
int storage_sd_status(Storage*s){assert(s==&storage);return sd_ok?FSE_OK:1;}
FlipperFormat* flipper_format_file_alloc(Storage*s){assert(s==&storage);formats++;return &format;}
bool flipper_format_file_open_existing(FlipperFormat*f,const char*p){
 assert(f==&format&&!strcmp(p,"/ext/subghz/assets/last_subghz.settings"));return open_ok;
}
void flipper_format_file_close(FlipperFormat*f){assert(f==&format);}
void flipper_format_free(FlipperFormat*f){assert(f==&format&&formats);formats--;}
FuriString* furi_string_alloc(void){strings++;return calloc(1,sizeof(FuriString));}
void furi_string_free(FuriString*s){assert(strings);strings--;free(s);}
const char* furi_string_get_cstr(FuriString*s){return s->text;}
bool flipper_format_read_header(FlipperFormat*f,FuriString*s,uint32_t*v){
 assert(f==&format);*v=version;snprintf(s->text,sizeof(s->text),"%s",header_type);return header_ok;
}
bool flipper_format_rewind(FlipperFormat*f){assert(f==&format);rewinds++;return true;}
bool flipper_format_read_uint32(FlipperFormat*f,const char*k,uint32_t*v,size_t n){
 assert(f==&format&&n==1);
 if(!strcmp(k,"IgnoreFilter")){
  if(partial_failure){*v=UINT32_MAX;return false;}
  if(ignore_missing)return false;
 }
 if(fields_missing)return false;
 for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++)
  if(!strcmp(k,entries[i].name)){*v=entries[i].value;return true;}
 return false;
}
bool flipper_format_read_float(FlipperFormat*f,const char*k,float*v,size_t n){
 assert(f==&format&&n==1);(void)k;if(fields_missing)return false;*v=-82.0f;return true;
}
bool flipper_format_read_bool(FlipperFormat*f,const char*k,bool*v,size_t n){
 assert(f==&format&&n==1);(void)k;if(fields_missing)return false;*v=true;return true;
}
bool flipper_format_read_string(FlipperFormat*f,const char*k,FuriString*s){
 assert(f==&format);(void)k;if(fields_missing)return false;
 snprintf(s->text,sizeof(s->text),"Feron,Princeton");return true;
}
bool furi_hal_subghz_is_tx_allowed(uint32_t f){return f==433920000||f==315000000;}
bool furi_hal_subghz_is_frequency_valid(uint32_t f){return furi_hal_subghz_is_tx_allowed(f);}
/* Portable strlcpy for the host fixture (not a replacement in firmware). */
size_t strlcpy(char*dst,const char*src,size_t size){
 size_t n=strlen(src);if(size){size_t copy=n<size?n:size-1;memcpy(dst,src,copy);dst[copy]=0;}return n;
}
'''


def settings_support(stem):
    directory = SETTINGS[stem]
    body = source(f"{directory}/subghz_last_settings.c")
    # Includes all actual local defines and Standard filter normalization code.
    definitions = body[:body.index("SubGhzLastSettings* subghz_last_settings_alloc(")]
    return (SETTINGS_PRELUDE
            + enum_source("lib/subghz/types.h", "SubGhzProtocolFlag")
            + enum_source("lib/subghz/protocols/plugin_registry.h", "SubGhzProtocolPackGroup")
            + source(f"{directory}/subghz_last_settings.h") + "\n" + definitions
            + f"\n#define subghz_last_settings_load subghz_last_settings_load_{stem}\n"
            + function(body, "void subghz_last_settings_load(") + r'''
static void balanced(void){assert(!strings&&!formats&&!records);}
static void load(SubGhzLastSettings*s){subghz_last_settings_load(s,5);balanced();}
''')


class November19ReceiveTests(unittest.TestCase):
    def test_feron_keeps_all_commands_but_rejects_corrupt_command_and_inverse(self):
        run_c(feron_support() + r'''
int main(void){
 const uint16_t ids[]={0,0x6338,0xffff};
 for(unsigned id=0;id<3;id++)for(unsigned command=0;command<256;command++){
  uint32_t data=((uint32_t)ids[id]<<16)|(command<<8)|(command^0xffu);
  assert(decode(data,32,true)==1);
  for(unsigned bit=0;bit<16;bit++)assert(decode(data^(1u<<bit),32,true)==0);
  /* The check byte covers the command, not the device ID. */
  assert(decode(data^0x80000000u,32,true)==1);
  assert(decode(data,32,false)==0);
 }
 return 0;
}
''')

    def test_feron_rejects_wrong_lengths_and_recovers_for_next_frame(self):
        run_c(feron_support() + r'''
int main(void){
 for(unsigned bits=1;bits<=320;bits++)
  assert(decode(0x6338847bu,bits,true)==(bits==32));
 FUZZ_DECODER(SubGhzProtocolDecoderFeron,subghz_protocol_decoder_feron_feed,3);
 SubGhzProtocolDecoderFeron d={.base={.callback=capture}};expected=0x6338847b;received=0;
 for(unsigned frame=0;frame<3;frame++){
  uint32_t data=frame==1?0x6338847a:0x6338847b;
  subghz_protocol_decoder_feron_feed(&d,false,4500);
  for(unsigned bit=32;bit;bit--){
   bool one=(data>>(bit-1))&1u;
   subghz_protocol_decoder_feron_feed(&d,true,one?750:350);
   subghz_protocol_decoder_feron_feed(&d,false,bit==1?500:one?350:750);
  }
  subghz_protocol_decoder_feron_feed(&d,true,500);
 }
 assert(received==2);return 0;
}
''')

    def test_feron_repository_raw_capture_still_decodes(self):
        samples = []
        for line in (ROOT / "applications/debug/unit_tests/resources/unit_tests/subghz/feron_raw.sub").read_text().splitlines():
            if line.startswith("RAW_Data:"):
                samples.extend(map(int, line.split(":", 1)[1].split()))
        self.assertGreater(len(samples), 64)
        run_c(feron_support() + "\nstatic const int32_t samples[]={"
              + ",".join(map(str, samples)) + r'''};
int main(void){
 SubGhzProtocolDecoderFeron d={.base={.callback=capture}};expected=0x6338847b;
 for(unsigned i=0;i<COUNT_OF(samples);i++){
  int32_t sample=samples[i];assert(sample&&sample!=INT32_MIN);
  subghz_protocol_decoder_feron_feed(&d,sample>0,sample>0?sample:-sample);
 }
 assert(received>0);return 0;
}
''')

    def test_settings_standard_mask_and_read_failures(self):
        self.check_settings("standard")

    def test_settings_arf_mask_and_read_failures(self):
        self.check_settings("arf")

    def check_settings(self, stem):
        run_c(settings_support(stem) + r'''
int main(void){
 const uint32_t flags[]={SubGhzProtocolFlag_ReversRB2,SubGhzProtocolFlag_Alarms,
  SubGhzProtocolFlag_Sensors,SubGhzProtocolFlag_Princeton,SubGhzProtocolFlag_NiceFlorS};
 const uint32_t mask=0xf800u;uint32_t* ignore=&entries[7].value;
 SubGhzLastSettings expected={0};load(&expected);
 assert(expected.ignore_filter==0&&expected.filter==1026);
 assert(expected.frequency==433920000&&expected.preset_index==1);
 assert(expected.raw_frequency==315000000&&expected.raw_preset_index==4);
 assert(!strcmp(expected.protocol_filter,"Feron,Princeton"));
 for(unsigned subset=0;subset<32;subset++)for(unsigned foreign=0;foreign<33;foreign++){
  uint32_t selected=0;for(unsigned bit=0;bit<5;bit++)if(subset&(1u<<bit))selected|=flags[bit];
  uint32_t unknown=foreign==32?UINT32_MAX:(UINT32_C(1)<<foreign);
  *ignore=selected|unknown;SubGhzLastSettings actual={0};load(&actual);
  assert(actual.ignore_filter==((*ignore)&mask));
  expected.ignore_filter=actual.ignore_filter;assert(!memcmp(&expected,&actual,sizeof(actual)));
  /* Loading is read-only: the persisted test input is never rewritten. */
  assert(*ignore==(selected|unknown));
 }
 SubGhzLastSettings reused={0};reused.ignore_filter=UINT32_MAX;
 ignore_missing=true;load(&reused);assert(reused.ignore_filter==0);ignore_missing=false;
 reused.ignore_filter=UINT32_MAX;partial_failure=true;load(&reused);
 assert(reused.ignore_filter==0);partial_failure=false;assert(rewinds>0);
 for(unsigned failure=0;failure<5;failure++){
  reused.ignore_filter=UINT32_MAX;
  sd_ok=failure!=0;open_ok=failure!=1;header_ok=failure!=2;
  version=failure==3?2:3;header_type=failure==4?"unrelated settings":"Flipper SubGhz Last Setting File";
  load(&reused);assert(reused.ignore_filter==0);
 }
 sd_ok=open_ok=header_ok=true;version=3;header_type="Flipper SubGhz Last Setting File";
 fields_missing=true;load(&reused);assert(reused.ignore_filter==0);fields_missing=false;
 entries[0].value=0;entries[1].value=UINT32_MAX;entries[2].value=1;entries[3].value=UINT32_MAX;
 load(&reused);assert(reused.frequency==433920000&&reused.preset_index==1);
 assert(reused.raw_frequency==reused.frequency&&reused.raw_preset_index==reused.preset_index);
 entries[6].value=UINT32_MAX;load(&reused);assert(reused.protocol_pack_group==SubGhzProtocolPackGroupCore);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
