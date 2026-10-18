"""Offline Nice framing checks: synthetic inputs, no radio or receiver enrollment."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class NiceRawPassiveTests(unittest.TestCase):
    def test_standard_handoff_restores_pack_and_checks_installation_first(self):
        saved = (ROOT / "applications/main/subghz/scenes/subghz_scene_saved_menu.c").read_text()
        more = (ROOT / "applications/main/subghz/scenes/subghz_scene_more_raw.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define RECORD_STORAGE "storage"
#define RECORD_LOADER "loader"
#define EXT_PATH(p) "/ext/" p
enum {LoaderDeferredLaunchFlagGui};
typedef int Storage;typedef int Loader;typedef const char FuriString;
typedef struct {void *dialogs,*scene_manager,*view_dispatcher;FuriString* file_path;} SubGhz;
static bool restore=true,installed=true,stopped;
static unsigned phase,queued,errors;
const char* furi_string_get_cstr(FuriString* s){return s;}
void* furi_record_open(const char* r){static int record;(void)r;return &record;}
void furi_record_close(const char* r){(void)r;}
bool storage_file_exists(Storage* s,const char* p){assert(s&&strstr(p,"nice_o_code.fap"));return installed;}
void dialog_message_show_storage_error(void* d,const char* s){(void)d;assert(s);errors++;}
bool subghz_scene_decode_raw_cleanup(SubGhz* s){assert(s&&phase==0);phase=1;return restore;}
void loader_clear_launch_queue(Loader* l){assert(l&&phase==1&&!queued);}
void loader_enqueue_launch(Loader* l,const char* target,const char* args,int flags){assert(l&&flags==LoaderDeferredLaunchFlagGui&&phase==1);
 if(queued==0){assert(!strcmp(target,"/ext/apps/Sub-GHz/nice_o_code.fap"));assert(!strcmp(args,"/ext/subghz/own sample.sub"));}
 else{assert(queued==1&&!strcmp(target,"Sub-GHz")&&!args);}queued++;}
void scene_manager_stop(void* m){(void)m;assert(queued==2);stopped=true;}
void view_dispatcher_stop(void* m){(void)m;assert(stopped);}
''' + function(saved, "bool subghz_launch_saved_analyzer(") + "\n" +
            function(more, "static bool subghz_scene_more_raw_analyze_nice(") + r'''
int main(void){
 SubGhz app={.file_path="/ext/subghz/own sample.sub"};
 restore=false;assert(subghz_scene_more_raw_analyze_nice(&app));assert(errors==1&&!queued&&!stopped);
 phase=0;errors=0;restore=true;installed=false;assert(subghz_scene_more_raw_analyze_nice(&app));assert(errors==1&&!queued&&!stopped);
 phase=0;errors=0;installed=true;assert(subghz_scene_more_raw_analyze_nice(&app));assert(!errors&&queued==2&&stopped);
 return 0;
}
''')

    def test_certificate_crc_marker_and_every_single_bit_corruption(self):
        header = (ROOT / "applications/system/nice_o_code/nice_o_certificate.h").as_posix()
        native('#include "' + header + '"\n' + r'''
#include <assert.h>
#include <stddef.h>
/* A separate bit-at-a-time reference builds synthetic fixtures, never RF. */
static unsigned crc(const uint8_t* p,unsigned first,unsigned last){
 unsigned state=255;
 for(unsigned n=first;n<last;n++){unsigned in=(p[n/8]>>(7-n%8))&1;
 unsigned carry=(state&1)^in;state/=2;if(carry)state^=0x97;}
 unsigned out=0;for(unsigned n=0;n<8;n++)out=out*2+((state>>n)&1);return out;
}
int main(void){
 const uint64_t body=0x123456789abcdULL;
 const uint32_t certificates[]={0,0xabcdef,0xffffff};
 for(unsigned c=0;c<3;c++){
  uint8_t p[15]={0};for(unsigned n=0;n<7;n++)p[n]=body>>(48-n*8);
  p[7]=0xcf;p[8]=((2^p[7])&15)<<4;
  unsigned a=crc(p,4,68);p[8]|=a>>4;p[9]=(a<<4)|14;
  uint32_t masked=(certificates[c]^(uint32_t)body)&0xffffff;
  p[10]=0x60|(masked>>20);p[11]=masked>>12;p[12]=masked>>4;p[13]=masked<<4;
  a=crc(p,68,108);p[13]|=a>>4;p[14]=a<<4;
  uint64_t tail=0;for(unsigned n=7;n<15;n++)tail=tail*256+p[n];tail>>=4;
  uint32_t value=0x555555;
  assert(nice_o_certificate_decode(body,tail,&value)&&value==certificates[c]);
  assert(!nice_o_certificate_decode(body,tail,NULL));
  assert(!nice_o_certificate_decode(body|1ULL<<52,tail,&value));
  assert(!nice_o_certificate_decode(body,tail|1ULL<<60,&value));
  for(unsigned bit=0;bit<112;bit++){
   uint64_t bad_body=body,bad_tail=tail;
   if(bit<52)bad_body^=1ULL<<bit;else bad_tail^=1ULL<<(bit-52);
   value=0x555555;assert(!nice_o_certificate_decode(bad_body,bad_tail,&value));
   assert(value==0x555555);
  }
 }
 return 0;
}
''')

    def test_raw_lifecycle_and_budget_without_any_radio_api(self):
        source = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        reader = (ROOT / "lib/subghz/subghz_raw_decode.h").read_text()
        reader = "\n".join(line for line in reader.splitlines() if not line.startswith("#"))
        production = "\n".join(function(source, signature) for signature in (
            "static void nice_o_raw_end(", "static bool nice_o_raw_begin(", "static bool nice_o_raw_next("))
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define FSAM_READ 1
#define FSOM_OPEN_EXISTING 1
#define SUBGHZ_RAW_FILE_TYPE "Flipper SubGhz RAW File"
#define NICE_O_KEYSTORE "synthetic"
#define SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME "Nice FloR-S"
enum {SubGhzProtocolFlag_Decodable};
typedef struct {char data[256];} FuriString;
typedef int FlipperFormat;
typedef struct {bool running;unsigned remaining;bool end;} SubGhzFileEncoderWorker;
typedef struct {unsigned kind,duration;bool level;} LevelDuration;
typedef struct {void *storage,*environment,*receiver,*mutex;SubGhzFileEncoderWorker* raw_worker;FuriString* raw_progress_text;uint32_t frequency,raw_samples;uint8_t raw_progress;uint16_t key_bits;} NiceOCode;
static bool open_ok=true,header_ok=true,protocol_ok=true,start_ok=true;
static unsigned objects,feeds,resets,stops;
static uint32_t next_duration=500;
static int subghz_protocol_registry;
FuriString* furi_string_alloc(void){objects++;return calloc(1,sizeof(FuriString));}
void furi_string_free(FuriString* s){assert(s&&objects);objects--;free(s);}
const char* furi_string_get_cstr(FuriString* s){return s->data;}
bool furi_string_equal_str(FuriString* s,const char* p){return !strcmp(s->data,p);}
FlipperFormat* flipper_format_file_alloc(void* s){(void)s;objects++;return malloc(1);}
void flipper_format_free(FlipperFormat* f){objects--;free(f);}
void* flipper_format_get_raw_stream(FlipperFormat* f){return f;}
bool file_stream_open(void* f,const char* p,int mode,int open){assert(f&&p&&mode==FSAM_READ&&open==FSOM_OPEN_EXISTING);return open_ok;}
bool flipper_format_read_header(FlipperFormat* f,FuriString* s,uint32_t* v){(void)f;strcpy(s->data,header_ok?SUBGHZ_RAW_FILE_TYPE:"Bad");*v=1;return true;}
bool flipper_format_read_string(FlipperFormat* f,const char* key,FuriString* s){(void)f;assert(!strcmp(key,"Protocol"));strcpy(s->data,protocol_ok?"RAW":"Nice FloR-S");return true;}
bool flipper_format_rewind(FlipperFormat* f){(void)f;return true;}
bool flipper_format_read_uint32(FlipperFormat* f,const char* key,uint32_t* n,int count){(void)f;assert(!strcmp(key,"Frequency")&&count==1);*n=433920000;return true;}
bool furi_hal_subghz_is_frequency_valid(uint32_t f){return f==433920000;}
void* subghz_environment_alloc(void){objects++;return malloc(1);}
void subghz_environment_set_protocol_registry(void* e,void* r){assert(e&&r);}
void subghz_environment_set_nice_flor_s_rainbow_table_file_name(void* e,const char* p){assert(e&&p);}
void subghz_environment_free(void* e){objects--;free(e);}
void* subghz_receiver_alloc_init(void* e){assert(e);objects++;return malloc(1);}
void subghz_receiver_free(void* r){objects--;free(r);}
void subghz_receiver_set_filter(void* r,int f){assert(r);(void)f;}
void nice_o_rx_callback(void* r,void* b,void* c){(void)r;(void)b;(void)c;}
void subghz_receiver_set_rx_callback(void* r,void (*cb)(void*,void*,void*),void* c){assert(r&&cb&&c);}
void subghz_receiver_decode(void* r,bool l,uint32_t d){assert(r&&l&&d&&d<=1000000);feeds++;}
void subghz_receiver_reset(void* r){assert(r);resets++;}
SubGhzFileEncoderWorker* subghz_file_encoder_worker_alloc(void){objects++;return calloc(1,sizeof(SubGhzFileEncoderWorker));}
bool subghz_file_encoder_worker_start(SubGhzFileEncoderWorker* w,const char* p,const char* radio){assert(p&&!radio);w->running=start_ok;return start_ok;}
bool subghz_file_encoder_worker_is_running(SubGhzFileEncoderWorker* w){return w->running;}
void subghz_file_encoder_worker_stop(SubGhzFileEncoderWorker* w){assert(w->running);w->running=false;stops++;}
void subghz_file_encoder_worker_free(SubGhzFileEncoderWorker* w){assert(!w->running);objects--;free(w);}
void subghz_file_encoder_worker_get_text_progress(SubGhzFileEncoderWorker* w,FuriString* s){assert(w->running);strcpy(s->data,"125%");}
LevelDuration subghz_file_encoder_worker_get_level_duration(SubGhzFileEncoderWorker* w){if(w->end)return(LevelDuration){.kind=2};if(!w->remaining)return(LevelDuration){.kind=1};w->remaining--;return(LevelDuration){.level=true,.duration=next_duration};}
bool level_duration_is_reset(LevelDuration d){return d.kind==2;}
bool level_duration_is_wait(LevelDuration d){return d.kind==1;}
uint32_t level_duration_get_duration(LevelDuration d){return d.duration;}
bool level_duration_get_level(LevelDuration d){return d.level;}
enum {FuriWaitForever};
void furi_mutex_acquire(void* m,int t){(void)m;(void)t;}
void furi_mutex_release(void* m){(void)m;}
''' + reader + "\n" + production + r'''
int main(void){
 NiceOCode a={0};
 const char* bad[]={NULL,"x","/int/a.sub","/ext/subghz/../bad.sub","/ext/subghz/a.nfc","/ext/subghz/..\\bad.sub"};
 for(unsigned n=0;n<6;n++)assert(!nice_o_raw_begin(&a,bad[n])&&!objects);
 char huge[300];memset(huge,'a',299);huge[299]=0;assert(!nice_o_raw_begin(&a,huge)&&!objects);
 const char* good="/ext/subghz/own sample.sub";
 open_ok=false;assert(!nice_o_raw_begin(&a,good)&&!objects);open_ok=true;
 header_ok=false;assert(!nice_o_raw_begin(&a,good)&&!objects);header_ok=true;
 protocol_ok=false;assert(!nice_o_raw_begin(&a,good)&&!objects);protocol_ok=true;
 start_ok=false;assert(!nice_o_raw_begin(&a,good)&&!objects);start_ok=true;
 assert(nice_o_raw_begin(&a,good)&&objects==4);
 assert(!nice_o_raw_begin(&a,good)&&objects==4);
 assert(nice_o_raw_next(&a)&&!feeds&&!a.raw_samples);
 a.raw_worker->remaining=1000;assert(nice_o_raw_next(&a));
 assert(feeds==400&&a.raw_samples==400&&a.raw_progress==99);
 next_duration=1000001;a.raw_worker->remaining=2;assert(nice_o_raw_next(&a));
 assert(resets==2&&feeds==400);
 next_duration=0;a.raw_worker->remaining=1;assert(nice_o_raw_next(&a));assert(resets==3);
 a.raw_worker->end=true;assert(!nice_o_raw_next(&a));
 nice_o_raw_end(&a);assert(!objects&&stops==1&&!a.raw_worker&&!a.receiver&&!a.environment);
 nice_o_raw_end(&a);assert(!objects&&stops==1);
 return 0;
}
''')

    def test_all_72_bit_parcels_and_plain_nice_one(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
bool subghz_protocol_blocks_get_bit_array(uint8_t* p,unsigned bit){return(p[bit/8]>>(7-bit%8))&1;}
''' + function(source, "static uint32_t subghz_protocol_nice_one_crc(") + "\n" +
            function(source, "static void subghz_protocol_nice_one_get_data(uint8_t* p, uint8_t num_parcel, uint8_t hold_bit) {") + r'''
int main(void){
 const uint8_t expected[16]={0x8f,0x8e,0x8d,0x8c,0x84,0x85,0x86,0x87,0x8f,0x8c,0x89,0x8d,0x8c,0x8d,0x8e,0x8f};
 for(unsigned parcel=0;parcel<16;parcel++){
  for(unsigned release=0;release<2;release++){
   uint8_t p[10]={1,2,3,4,5,6,7};
   subghz_protocol_nice_one_get_data(p,parcel,4|release);
   assert(p[7]==expected[parcel]);
   assert((((p[8]>>4)^p[7])&3)==release);
   assert(((p[8]&15)<<4|(p[9]>>4))==subghz_protocol_nice_one_crc(p));
   uint8_t plain[10]={1,2,3,4,5,6,7};
   subghz_protocol_nice_one_get_data(plain,parcel,release);
   assert(plain[7]==(parcel^(parcel<4?0x8f:0x80)));
   assert((((plain[8]>>4)^plain[7])&3)==release);
  }
 }
 return 0;
}
''')

    def test_offline_decode_reuses_standard_reader_and_defers_launch(self):
        raw = (ROOT / "applications/main/subghz/scenes/subghz_scene_decode_raw.c").read_text()
        more = (ROOT / "applications/main/subghz/scenes/subghz_scene_more_raw.c").read_text()
        nice = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        self.assertIn("subghz_raw_decode_sample", raw)
        self.assertIn("subghz_raw_decode_sample", nice)
        self.assertIn("Nice O-Code", more)
        self.assertIn("NiceOSceneRaw", nice)
        self.assertIn("nice_o_raw_begin", nice)

    def test_passive_certificate_has_a_separate_checked_parser(self):
        header = ROOT / "applications/system/nice_o_code/nice_o_certificate.h"
        self.assertTrue(header.exists())
        text = header.read_text()
        self.assertIn("nice_o_certificate_decode", text)


if __name__ == "__main__":
    unittest.main()
