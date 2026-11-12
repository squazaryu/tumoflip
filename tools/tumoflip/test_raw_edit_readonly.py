"""Execute RAW Edit synthesis/import bodies; input files may never be writable."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / 'applications_user/subghz_raw_edit/subghz_raw_edit.c'

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define LOAD_HEAP_RESERVE 12288
#define SYNTH_YIELD_CAP 4096
#define MAX_KEY_FILE_BYTES 16384
typedef int Storage;
enum {FSAM_READ,FSOM_OPEN_EXISTING,SubGhzProtocolStatusOk,SubGhzProtocolPackGroupCore};
typedef struct {bool file;} Stream;
typedef struct {bool file;Stream stream;} FlipperFormat;
typedef struct {bool core;} SubGhzEnvironment;
typedef struct {bool core;} SubGhzProtocolRegistry;
typedef int SubGhzProtocolPackRegistry;
typedef struct {unsigned index;} SubGhzTransmitter;
typedef struct {bool level;uint32_t duration;} LevelDuration;
typedef struct {int16_t* data;size_t count,cap;int32_t total_us;bool truncated,out_of_memory,synthesized;} SubData;
static const SubGhzProtocolRegistry subghz_protocol_registry={false},core_registry={true};
static size_t input_size=256,free_heap=100000,max_block=100000;
static bool copy_ok=true,rewind_ok=true;
static unsigned input_writes,memory_writes,formats,streams,envs,transmitters,packs;
size_t memmgr_get_free_heap(void){return free_heap;}
size_t memmgr_heap_get_max_free_block(void){return max_block;}
FlipperFormat* flipper_format_file_alloc(Storage* s){(void)s;FlipperFormat* f=calloc(1,sizeof(*f));f->file=true;formats++;return f;}
FlipperFormat* flipper_format_string_alloc(void){FlipperFormat* f=calloc(1,sizeof(*f));formats++;return f;}
bool flipper_format_file_open_existing(FlipperFormat* f,const char* p){(void)f;(void)p;return true;}
void flipper_format_file_close(FlipperFormat* f){(void)f;}
void flipper_format_free(FlipperFormat* f){assert(f);formats--;free(f);}
Stream* file_stream_alloc(Storage* s){(void)s;streams++;Stream* p=calloc(1,sizeof(*p));p->file=true;return p;}
bool file_stream_open(Stream* s,const char* p,int access,int mode){(void)s;(void)p;assert(access==FSAM_READ && mode==FSOM_OPEN_EXISTING);return true;}
size_t stream_size(Stream* s){assert(s->file);return input_size;}
Stream* flipper_format_get_raw_stream(FlipperFormat* f){return &f->stream;}
size_t stream_copy_full(Stream* from,Stream* to){assert(from->file && !to->file);return copy_ok?input_size:input_size-1;}
bool stream_rewind(Stream* s){(void)s;return rewind_ok;}
void stream_free(Stream* s){streams--;free(s);}
SubGhzEnvironment* subghz_env_setup(void){envs++;return calloc(1,sizeof(SubGhzEnvironment));}
void subghz_environment_free(SubGhzEnvironment* e){assert(!transmitters);envs--;free(e);}
bool subghz_environment_load_keystore(SubGhzEnvironment* e,const char* p){(void)e;(void)p;return true;}
void subghz_environment_set_protocol_registry(SubGhzEnvironment* e,const SubGhzProtocolRegistry* r){e->core=r->core;}
SubGhzProtocolPackRegistry* subghz_protocol_pack_registry_alloc(const SubGhzProtocolRegistry* b,const char* p,int g){
    (void)b;assert(strcmp(p,"/ext/apps_data/subghz/plugins")==0 && g==SubGhzProtocolPackGroupCore);
    packs++;return malloc(sizeof(SubGhzProtocolPackRegistry));
}
const SubGhzProtocolRegistry* subghz_protocol_pack_registry_get(SubGhzProtocolPackRegistry* p){assert(p);return &core_registry;}
void subghz_protocol_pack_registry_free(SubGhzProtocolPackRegistry* p){assert(!envs && !transmitters);packs--;free(p);}
SubGhzTransmitter* subghz_transmitter_alloc_init(SubGhzEnvironment* e,const char* p){
    if(strcmp(p,"Faac TM")==0 && !e->core)return NULL;
    transmitters++;return calloc(1,sizeof(SubGhzTransmitter));
}
int subghz_transmitter_deserialize(SubGhzTransmitter* t,FlipperFormat* f){
    (void)t;if(f->file)input_writes++;else memory_writes++;return SubGhzProtocolStatusOk;
}
LevelDuration subghz_transmitter_yield(SubGhzTransmitter* t){
    const LevelDuration p[]={{false,14000},{true,400},{false,400},{true,800}};
    return t->index<12?p[t->index++%4]:(LevelDuration){false,0};
}
void subghz_transmitter_free(SubGhzTransmitter* t){transmitters--;free(t);}
bool level_duration_is_reset(LevelDuration d){return d.duration==0;}
uint32_t level_duration_get_duration(LevelDuration d){return d.duration;}
bool level_duration_get_level(LevelDuration d){return d.level;}
bool push_duration(SubData* s,int32_t v){
    int16_t* p=realloc(s->data,(s->count+1)*sizeof(*p));assert(p);s->data=p;s->data[s->count++]=v;return true;
}
'''


class RawReadOnlyTests(unittest.TestCase):
    def test_encoder_updates_only_a_bounded_memory_copy_and_releases_core_modules(self):
        source = PATH.read_text()
        helper = function(source, 'static FlipperFormat *fff_load_copy(') if 'static FlipperFormat *fff_load_copy(' in source else ''
        actual = function(source, 'static bool synthesize_via_transmitter(')
        native(PRELUDE + helper + actual + r'''
int main(void){
    (void)subghz_protocol_registry;
    Storage storage=0;SubData data={0};
    assert(synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));
    assert(input_writes==0 && memory_writes==1);free(data.data);
    data=(SubData){0};
    assert(synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","Faac TM",&data));
    assert(input_writes==0 && memory_writes==2);free(data.data);
    const size_t sizes[]={0,16385,UINT32_MAX};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){
        input_size=sizes[i];data=(SubData){0};
        assert(!synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));free(data.data);
    }
    input_size=256;free_heap=12500;data=(SubData){0};
    assert(!synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));
    free_heap=100000;max_block=256;data=(SubData){0};
    assert(!synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));
    max_block=100000;copy_ok=false;
    assert(!synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));
    copy_ok=true;rewind_ok=false;
    assert(!synthesize_via_transmitter(&storage,"/ext/subghz/owned.sub","KeeLoq",&data));
    assert(input_writes==0 && formats==0 && streams==0 && envs==0 && transmitters==0 && packs==0);
    return 0;
}
''')

    def test_merge_preserves_synthesized_guard_metadata(self):
        source = PATH.read_text()
        actual = function(source, 'static bool fill_sub_into(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef int Storage;
typedef enum {MergeJoinNone,MergeJoinManual,MergeJoinNative} MergeJoin;
typedef struct {int16_t* data;size_t count;bool synthesized;} SubData;
static bool fail_append;
bool fill_raw_into(Storage* s,const char* p,SubData* d,MergeJoin j){(void)s;(void)p;(void)d;(void)j;return true;}
bool load_sub(Storage* s,const char* p,SubData* d){(void)s;(void)p;d->data=calloc(2,sizeof(int16_t));d->count=2;d->synthesized=true;return true;}
bool merge_prepare_join(SubData* d,MergeJoin j,bool* skip){(void)d;(void)j;*skip=false;return true;}
bool append_sample(SubData* d,int32_t v){(void)v;d->count++;return !fail_append;}
''' + actual + r'''
int main(void){
    Storage s=0;SubData merged={0};
    assert(fill_sub_into(&s,"owned.sub",&merged,false,MergeJoinNone) && merged.synthesized);
    assert(fill_sub_into(&s,"raw.sub",&merged,true,MergeJoinManual) && merged.synthesized);
    merged=(SubData){0};fail_append=true;
    assert(!fill_sub_into(&s,"owned.sub",&merged,false,MergeJoinNone) && !merged.synthesized);
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
