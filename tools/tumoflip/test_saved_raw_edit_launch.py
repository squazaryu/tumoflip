"""Verify the saved RAW handoff with synthetic files and no radio or real SD."""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native as run_c


ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "applications/main/subghz/scenes/subghz_scene_more_raw.c"
EDITOR = ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"

SCENE_FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define EXT_PATH(s) "/ext/" s
#define SUBGHZ_APP_FOLDER "/ext/subghz"
#define RECORD_STORAGE "storage"
typedef char FuriString;
typedef int Storage, FlipperApplication, ElfApiInterface;
typedef struct {uint32_t app_version;} FlipperApplicationManifest;
typedef struct {int type;unsigned event;} SceneManagerEvent;
typedef struct {
    void *submenu,*scene_manager,*view_dispatcher,*dialogs;
    FuriString *file_path,*file_path_tmp;
    bool decode_raw_auto;
} SubGhz;
enum {SubGhzSceneStart,SubGhzSceneMoreRAW,SubGhzSceneReadRAW,SubGhzSceneDeleteRAW,
      SubGhzSceneSaveName,SubGhzSceneDecodeRAW,SubGhzViewIdMenu,
      SubGhzCustomEventManagerNoSet,SceneManagerEventTypeCustom,
      SubGhzRxKeyStateRAWLoad,SubGhzRxKeyStateRAWMore,
      FlipperApplicationPreloadStatusSuccess,FlipperApplicationPreloadStatusApiTooNew};

static int key_state=SubGhzRxKeyStateRAWLoad,status=FlipperApplicationPreloadStatusSuccess;
static FlipperApplicationManifest manifest={.app_version=(1u<<16)|9u};
static unsigned labels,launched,versions,open_records,allocated,dialogs,cleanups,next;
static bool present=true,restore=true,plugin=false,capture_exists=true;
static size_t max_block=32768,free_heap=32768;
const ElfApiInterface* firmware_api_interface;
static const char editor_path[]="/ext/apps/ARF Tools/subghz_raw_edit.fap";
static const char capture_path[]="/ext/subghz/Workshop test/Light.sub";
void* furi_record_open(const char* record){assert(!strcmp(record,RECORD_STORAGE));open_records++;static Storage s;return &s;}
void furi_record_close(const char* record){assert(!strcmp(record,RECORD_STORAGE)&&open_records);open_records--;}
bool storage_file_exists(Storage* s,const char* path){
    (void)s;
    return !strcmp(path,editor_path)?present:!strcmp(path,capture_path)&&capture_exists;
}
size_t memmgr_heap_get_max_free_block(void){return max_block;}
size_t memmgr_get_free_heap(void){return free_heap;}
FlipperApplication* flipper_application_alloc(Storage* s,const ElfApiInterface* api){
    (void)s;(void)api;assert(allocated==0);allocated++;static FlipperApplication a;return &a;
}
int flipper_application_preload_manifest(FlipperApplication* a,const char* path){
    assert(a&&allocated&&!strcmp(path,editor_path));versions++;return status;
}
const FlipperApplicationManifest* flipper_application_get_manifest(FlipperApplication* a){assert(a&&allocated);return &manifest;}
bool flipper_application_is_plugin(FlipperApplication* a){assert(a&&allocated);return plugin;}
void flipper_application_free(FlipperApplication* a){assert(a&&allocated==1);allocated--;}
bool subghz_file_available(SubGhz* a){(void)a;return capture_exists;}
bool subghz_path_is_file(FuriString* s){
    size_t n=strlen(s);return n>4&&!strcmp(s+n-4,".sub");
}
int subghz_rx_key_state_get(SubGhz* a){(void)a;return key_state;}
bool furi_string_start_with(FuriString* s,const char* prefix){return !strncmp(s,prefix,strlen(prefix));}
const char* furi_string_get_cstr(FuriString* s){return s;}
void furi_string_reset(FuriString* s){*s=0;}
void submenu_add_item(void* menu,const char* label,uint32_t index,void(*callback)(void*,uint32_t),void* context){
    (void)menu;(void)callback;(void)context;assert(index<8);
    if(!strcmp(label,"Edit RAW"))assert(index==5);
    labels|=1u<<index;
}
void submenu_set_selected_item(void* menu,unsigned id){(void)menu;(void)id;}
unsigned scene_manager_get_scene_state(void* scene,unsigned id){(void)scene;(void)id;return 0;}
void scene_manager_set_scene_state(void* scene,unsigned id,unsigned v){(void)scene;(void)id;(void)v;}
void scene_manager_next_scene(void* scene,unsigned id){(void)scene;next=id;}
bool scene_manager_search_and_switch_to_previous_scene(void* scene,unsigned id){(void)scene;(void)id;return true;}
void scene_manager_stop(void* scene){(void)scene;}
void view_dispatcher_stop(void* view){(void)view;}
void view_dispatcher_switch_to_view(void* view,unsigned id){(void)view;(void)id;}
void view_dispatcher_send_custom_event(void* view,unsigned id){(void)view;(void)id;}
void dialog_message_show_storage_error(void* d,const char* message){(void)d;assert(message);dialogs++;}
bool subghz_scene_decode_raw_cleanup(SubGhz* a){assert(a);cleanups++;return restore;}
bool subghz_launch_saved_analyzer(SubGhz* a,const char* target){
    assert(!open_records&&!allocated&&cleanups);
    if(!strcmp(target,editor_path)){assert(!strcmp(a->file_path,capture_path));launched++;}
    return true;
}
'''

EDITOR_FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_DIALOGS "dialogs"
#define RECORD_STORAGE "storage"
typedef int Storage, DialogsApp;
static unsigned opens,closes,runs,errors;
static const char* expected;
void* furi_record_open(const char* name){assert(name);opens++;static int record;return &record;}
void furi_record_close(const char* name){assert(name&&closes<opens);closes++;}
void dialog_message_show_storage_error(DialogsApp* d,const char* text){assert(d&&text);errors++;}
void run_editor(Storage* s,DialogsApp* d,const char* path){assert(s&&d&&!strcmp(path,expected));runs++;}
'''

LOAD_FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_SAMPLES 24000
#define SUBGHZ_RAW_FILE_TYPE "Flipper SubGhz RAW File"
typedef int Storage;
enum {FSAM_READ,FSOM_OPEN_EXISTING,FSE_OK,FSE_INTERNAL};
typedef struct {char data[9000];size_t length;} FuriString;
typedef struct {int16_t* data;size_t count,cap;int32_t total_us;uint32_t frequency;
    char preset[48];bool out_of_memory,truncated,synthesized;} SubData;
typedef struct {size_t pos;bool opened;} File;
static const char* input;
static bool can_open=true,alloc_ok=true,g_normalize_jitter,read_error=false;
static unsigned files,strings,synthesis,opened;
FuriString* furi_string_alloc(void){strings++;return calloc(1,sizeof(FuriString));}
void furi_string_free(FuriString* s){strings--;free(s);}
size_t furi_string_size(const FuriString* s){return s->length;}
void furi_string_reset(FuriString* s){s->length=0;s->data[0]=0;}
void furi_string_push_back(FuriString* s,char c){
    assert(s->length+1<sizeof(s->data));s->data[s->length++]=c;s->data[s->length]=0;
}
const char* furi_string_get_cstr(const FuriString* s){return s->data;}
File* storage_file_alloc(Storage* s){assert(s);files++;return calloc(1,sizeof(File));}
void storage_file_free(File* f){assert(!f->opened);files--;free(f);}
bool storage_file_open(File* f,const char* p,int access,int mode){
    assert(!strcmp(p,"/ext/subghz/Light.sub")&&access==FSAM_READ&&mode==FSOM_OPEN_EXISTING);
    opened++;return f->opened=can_open;
}
void storage_file_close(File* f){assert(f->opened);f->opened=false;}
uint64_t storage_file_size(File* f){assert(f->opened);return strlen(input);}
size_t storage_file_read(File* f,void* buffer,size_t size){
    assert(f->opened);size_t count=strlen(input+f->pos);if(count>size)count=size;
    memcpy(buffer,input+f->pos,count);f->pos+=count;return count;
}
int storage_file_get_error(File* f){assert(f->opened);return read_error?FSE_INTERNAL:FSE_OK;}
void* safe_malloc(size_t n){return alloc_ok?malloc(n):NULL;}
void* safe_realloc(void* p,size_t n){return realloc(p,n);}
bool next_raw_value(const char** s,int32_t* d){
    char* end;long v=strtol(*s,&end,10);if(end==*s)return false;*d=(int32_t)v;*s=end;return true;
}
bool push_duration(SubData* d,int32_t v){assert(d->count<d->cap);d->data[d->count++]=(int16_t)v;return true;}
bool synthesize_via_transmitter(Storage* s,const char* path,const char* protocol,SubData* d){
    (void)s;(void)path;assert(!strcmp(protocol,"Demo"));synthesis++;
    return push_duration(d,350)&&push_duration(d,-750);
}
void normalize_jitter(SubData* d){assert(d);}
int32_t iabs32(int32_t n){return n<0?-n:n;}
'''


def optional_functions(source, *signatures):
    return "\n".join(function(source, name) for name in signatures if name in source) + "\n"


class SavedRawEditLaunchTests(unittest.TestCase):
    def test_saved_menu_gates_editor_compatibility_and_preserves_existing_actions(self):
        source = SCENE.read_text()
        definitions = source[source.index("enum SubmenuIndex"):
                             source.index("void subghz_scene_more_raw_submenu_callback")]
        definitions = "\n".join(re.findall(r"(?m)^#define RAW_EDIT_[^\n]+", source)) + "\n" + definitions
        production = optional_functions(
            source, "void subghz_scene_more_raw_submenu_callback(",
            "static bool subghz_scene_more_raw_can_edit(",
            "void subghz_scene_more_raw_on_enter(",
            "static bool subghz_scene_more_raw_analyze_nice(",
            "bool subghz_scene_more_raw_on_event(",
        )
        run_c(SCENE_FIXTURE + definitions + production + r'''
int main(void){
    char capture[80];strcpy(capture,capture_path);
    char previous[]="old";
    SubGhz s={.file_path=capture,.file_path_tmp=previous};
    for(unsigned variant=0;variant<9;variant++){
        key_state=variant==1?SubGhzRxKeyStateRAWMore:SubGhzRxKeyStateRAWLoad;
        present=variant!=2;plugin=variant==3;
        status=variant==4?FlipperApplicationPreloadStatusApiTooNew:FlipperApplicationPreloadStatusSuccess;
        manifest.app_version=(1u<<16)|(variant==5?8u:9u);
        free_heap=variant==6?0:32768;max_block=variant==7?0:32768;
        strcpy(capture,variant==8?"/ext/Backup/Light.sub":capture_path);
        labels=0;subghz_scene_more_raw_on_enter(&s);
        assert(labels==(variant==0?63u:31u)&&!open_records&&!allocated);
    }
    strcpy(capture,capture_path);
    SceneManagerEvent edit={.type=SceneManagerEventTypeCustom,.event=5};
    restore=false;capture_exists=false;
    assert(subghz_scene_more_raw_on_event(&s,edit)&&!launched&&!cleanups);
    capture_exists=true;present=false;
    assert(subghz_scene_more_raw_on_event(&s,edit)&&!launched&&!cleanups);
    present=true;
    assert(subghz_scene_more_raw_on_event(&s,edit)&&!launched&&cleanups==1);
    restore=true;
    assert(subghz_scene_more_raw_on_event(&s,edit)&&launched==1&&cleanups==2);
    assert(!open_records&&!allocated&&versions&&dialogs==2);
    assert(subghz_scene_more_raw_on_event(&s,(SceneManagerEvent){.type=SceneManagerEventTypeCustom,.event=2}));
    assert(next==SubGhzSceneSaveName&&!*previous);
    return 0;
}
''')

    def test_argument_dispatch_rejects_non_capture_paths_without_a_browser_fallback(self):
        source = EDITOR.read_text()
        macros = "\n".join(re.findall(r"(?m)^#define (?:SUBGHZ_DIR|RAW_EDIT_\w+) [^\n]+", source))
        production = optional_functions(
            source, "static bool raw_edit_capture_path_is_valid(",
            "static bool raw_edit_handle_launch_argument(",
        )
        run_c(EDITOR_FIXTURE + "\n" + macros + "\n" + production + r'''
bool raw_edit_handle_launch_argument(const char*);
int main(void){
    assert(!raw_edit_handle_launch_argument(NULL)&&!raw_edit_handle_launch_argument("")&&!opens);
    expected="/ext/subghz/Workshop test/Light.sub";
    assert(raw_edit_handle_launch_argument(expected)&&runs==1&&opens==closes);
    const char* wrong[]={"Light.sub","/int/Light.sub","/ext/subghz2/Light.sub",
        "/ext/subghz/./Light.sub","/ext/subghz/../Light.sub","/ext/subghz/.. /Light.sub",
        "/ext/subghz//Light.sub","/ext/subghz/Bad\\Path.sub",
        "/ext/subghz/Line\nbreak.sub","/ext/subghz/Light.txt","/ext/subghz/.sub"};
    for(size_t i=0;i<sizeof(wrong)/sizeof(wrong[0]);i++){
        assert(raw_edit_handle_launch_argument(wrong[i])&&runs==1&&opens==closes);
    }
    char long_path[600];memset(long_path,'A',sizeof(long_path));
    memcpy(long_path,"/ext/subghz/",12);memcpy(long_path+sizeof(long_path)-5,".sub",5);
    assert(raw_edit_handle_launch_argument(long_path)&&runs==1&&opens==closes);
    assert(errors==12);
    return 0;
}
''')

    def test_exact_file_is_read_only_and_direct_load_never_dispatches_key_conversion(self):
        source = EDITOR.read_text()
        reader = source[source.rindex("typedef struct", 0, source.index("} LineReader;")):
                        source.index("static bool append_sample(")]
        definitions = "\n".join(re.findall(r"(?m)^#define RAW_EDIT_\w+ [^\n]+", source))
        loader = function(source, "static bool load_sub(")
        run_c(LOAD_FIXTURE + "\n" + definitions + "\n" + reader + loader + r'''
static bool load(const char* content,bool direct){
    input=content;SubData d;Storage s=1;
    bool ok=load_sub(&s,"/ext/subghz/Light.sub",&d,direct);
    if(ok)assert(d.count==2&&d.total_us==1100);
    if(direct)assert(!d.synthesized);
    free(d.data);assert(!files&&!strings);
    return ok;
}
int main(void){
    const char* raw="Filetype: Flipper SubGhz RAW File\r\nVersion: 1\r\n"
                    "Frequency: 433920000\r\nPreset: FuriHalSubGhzPresetOok650Async\r\n"
                    "Protocol: RAW\r\nRAW_Data: 350 -750\r\n";
    assert(load(raw,true));
    assert(!load("Filetype: Flipper SubGhz RAW File\nVersion: 2\nProtocol: RAW\nRAW_Data: 350 -750\n",true));
    assert(!load("Filetype: Flipper SubGhz RAW File\nVersion: 1\nProtocol: Demo\n",true));
    assert(!load("Filetype: Flipper SubGhz RAW File\nVersion: 1\nRAW_Data: 350 -750\n",true));
    assert(!load("Filetype: Flipper SubGhz RAW File\nVersion: 1\nProtocol: RAW\n",true));
    assert(!load("Filetype: Another File\nVersion: 1\nProtocol: RAW\nRAW_Data: 350 -750\n",true));
    assert(synthesis==0);
    assert(load("Protocol: Demo\n",false)&&synthesis==1);
    read_error=true;assert(!load(raw,true));read_error=false;
    alloc_ok=false;assert(!load(raw,true));alloc_ok=true;
    can_open=false;assert(!load(raw,true));can_open=true;
    char overlong[8500];size_t n=strlen(raw);strcpy(overlong,raw);
    memset(overlong+n,'X',sizeof(overlong)-n-1);overlong[sizeof(overlong)-1]=0;
    assert(!load(overlong,true)&&synthesis==1&&opened==11);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
