"""Own LF observations/dumps; no reader credential capture or tag commands."""
from pathlib import Path
from tools.tumoflip.test_hotplug_assets import function
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'applications/system/device_library'


def production():
    path = APP / 'varta_inspect.c'
    return ('#include <assert.h>\n#include <string.h>\n'
            + f'#include "{APP / "varta_inspect.h"}"\n'
            + (path.read_text().replace('#include "varta_inspect.h"', '') if path.exists() else '')
            + r'''
static uint64_t encode(const uint8_t id[5]){uint64_t f=0x1FF;unsigned columns=0;
 for(unsigned i=0;i<10;i++){unsigned n=(id[i/2]>>(i%2?0:4))&15U,p=0;columns^=n;
  for(unsigned b=0;b<4;b++){p^=(n>>b)&1U;}
  f=(f<<5)|(n<<1)|p;}
 return (f<<5)|(columns<<1);}
''')


class VartaInspectionTests(unittest.TestCase):
    def test_saved_files_open_read_only_and_ignore_password_block(self):
        source = (APP / 'varta_app.c').read_text()
        bodies = function(source, 'static bool varta_app_load_dump(') + function(source, 'static bool varta_app_load_rfid(')
        native(production() + r'''
#include <stdio.h>
#include <stdlib.h>
typedef int Storage,ProtocolDict;
typedef struct {char text[128];}FuriString;
typedef int FlipperFormat;
typedef struct {Storage*storage;ProtocolDict*dict;FuriString*path;VartaInspection result;}VartaApp;
enum {FSAM_READ,FSOM_OPEN_EXISTING};
static bool dump=true,closed=true;static uint32_t version=1,count=5;static unsigned password_reads;
static uint32_t words[8];static const char*read_list="0 1 2 6 7",*key_type="EM4100";
static FlipperFormat*flipper_format_file_alloc(Storage*s){(void)s;return calloc(1,sizeof(FlipperFormat));}
static void*flipper_format_get_raw_stream(FlipperFormat*f){return f;}
static bool file_stream_open(void*f,const char*p,int access,int mode){assert(f&&p&&access==FSAM_READ&&mode==FSOM_OPEN_EXISTING);return true;}
static FuriString*furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static const char*furi_string_get_cstr(FuriString*s){return s->text;}
static int furi_string_cmp_str(FuriString*s,const char*t){return strcmp(s->text,t);}
static void furi_string_free(FuriString*s){free(s);}
static void flipper_format_free(FlipperFormat*f){free(f);}
static bool flipper_format_read_header(FlipperFormat*f,FuriString*s,uint32_t*v){assert(f);strcpy(s->text,dump?"Flipper T5577 dump":"Flipper RFID key");*v=version;return true;}
static bool flipper_format_read_string(FlipperFormat*f,const char*key,FuriString*s){assert(f);
 if(!strcmp(key,"Page 0 blocks read")){strcpy(s->text,read_list);return true;}
 if(!strcmp(key,"Key type")){strcpy(s->text,key_type);return true;}return false;}
static bool flipper_format_rewind(FlipperFormat*f){assert(f);return true;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*key,uint8_t*out,unsigned size){assert(f);
 unsigned block=0;if(sscanf(key,"Page 0 Block %u",&block)==1){assert(size==4&&block<8);if(block==7){password_reads++;return false;}
  for(unsigned i=0;i<4;i++){out[i]=words[block]>>(24-8*i);}return true;}
 if(!strcmp(key,"Data")){assert(size==5);memcpy(out,(uint8_t[]){3,0,1,2,3},5);return true;}return false;}
static bool flipper_format_file_close(FlipperFormat*f){assert(f);return closed;}
static bool flipper_format_get_value_count(FlipperFormat*f,const char*key,uint32_t*n){assert(f);*n=!strcmp(key,"Data")?count:4;return true;}
static const char*protocol_dict_get_name(ProtocolDict*d,unsigned p){(void)d;static const char*names[]={"EM4100","EM4100/32","EM4100/16"};assert(p<3);return names[p];}
''' + bodies + r'''
int main(void){FuriString path={"/ext/lfrfid/own.t5577"};VartaApp app={.path=&path};
 uint8_t id[]={3,0,1,2,3};uint64_t frame=encode(id);words[0]=0x00148050;words[1]=frame>>32;words[2]=frame;words[6]=0xFFAD2D00;
 assert(varta_app_load_dump(&app)&&app.result.status==VartaDumpDecoded&&!password_reads);
 read_list="0 6";assert(varta_app_load_dump(&app)&&app.result.status==VartaMissingBlocks);
 read_list="4294967299";assert(!varta_app_load_dump(&app));
 dump=false;assert(varta_app_load_rfid(&app)&&app.result.status==VartaIdOnly&&app.result.id[0]==3);
 key_type="I40134";assert(!varta_app_load_rfid(&app));key_type="EM4100";
 count=6;assert(!varta_app_load_rfid(&app));count=5;
 version=2;assert(!varta_app_load_rfid(&app));version=1;
 closed=false;assert(!varta_app_load_rfid(&app));return 0;}
''')

    def test_external_diagnostic_has_cancel_ownership_and_no_tag_write_or_password_path(self):
        path = APP / 'varta_app.c'
        self.assertTrue(path.exists(), 'missing Varta diagnostic UI')
        source = path.read_text()
        for token in ('lfrfid_worker_stop_thread', 'subghz_radio_broker_release',
                      'FSAM_READ', 'VartaMissingBlocks', 'field_report_save'):
            self.assertIn(token, source)
        self.assertNotRegex(source, r'(t5577_\w+|lfrfid_worker_write\w*|lfrfid_worker_emulate\w*)\s*\(')
        self.assertNotIn('VARTA_PASSWORD', source)
        self.assertIn('15000U', source)

    def test_em4100_frame_all_single_bit_corruptions_and_atomic_result(self):
        native(production() + r'''
int main(void){uint8_t id[]={3,0,0x12,0x34,0x56},out[5];uint64_t f=encode(id);
 assert(varta_inspect_frame(f>>32,f,out)&&!memcmp(id,out,5));
 for(unsigned i=0;i<64;i++){uint64_t bad=f^(1ULL<<i);memset(out,0xAA,5);assert(!varta_inspect_frame(bad>>32,bad,out));assert(out[0]==0xAA);}
 uint32_t random=3;for(unsigned k=0;k<10000;k++){for(unsigned i=0;i<5;i++){random=random*1664525+1013904223;id[i]=random>>24;}f=encode(id);assert(varta_inspect_frame(f>>32,f,out)&&!memcmp(out,id,5));}
 return 0;}
''')

    def test_prefix_is_not_authenticity_and_read_mask_is_not_padding(self):
        native(production() + r'''
int main(void){uint8_t mask;assert(varta_inspect_read_list("0 1 2 6 7",&mask)&&mask==0xC7);
 assert(!varta_inspect_read_list("4294967299",&mask));assert(!varta_inspect_read_list("1,2",&mask));assert(!varta_inspect_read_list("1 1",&mask));
 uint8_t id[]={4,0,1,2,3};VartaInspection v;varta_inspect_id(&v,id,64);assert(v.status==VartaIdOnly&&v.known_prefix&&!v.has_marker);
 uint64_t f=encode(id);uint32_t blocks[8]={0x00148050,f>>32,f,0,0,0,0xFFAD2D00,0};
 varta_inspect_dump(&v,blocks,0);assert(v.status==VartaMissingBlocks&&!v.has_marker);
 varta_inspect_dump(&v,blocks,0x47);assert(v.status==VartaDumpDecoded&&v.known_prefix&&v.config_matches&&v.marker_matches);
 blocks[6]^=1;varta_inspect_dump(&v,blocks,0x47);assert(v.has_marker&&!v.marker_matches);
 blocks[1]^=1;varta_inspect_dump(&v,blocks,0x47);assert(v.status==VartaBadFrame);return 0;}
''')


if __name__ == '__main__': unittest.main()
