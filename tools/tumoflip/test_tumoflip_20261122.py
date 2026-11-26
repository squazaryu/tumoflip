"""Native checks of BLE callback lifetime and read-only per-object link checks."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native as run_c

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"
HAL = ROOT / "targets/f7/furi_hal/furi_hal_bt.c"
BT = ROOT / "applications/services/bt/bt_service/bt.c"
INCLUDES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
'''


def app_source(name):
    path = APP / name
    return re.sub(r"(?m)^#(?:include|pragma).*\n", "", path.read_text()) if path.exists() else ""


class TumoflipNovember22Tests(unittest.TestCase):
    def test_storage_listener_is_restored_before_each_new_radio_stack_starts(self):
        source = HAL.read_text()
        state = source[source.index("typedef struct {"):source.index("static FuriHalBleProfileBase*")]
        functions = "\n".join(function(source, sig) for sig in (
            "void furi_hal_bt_init(", "void furi_hal_bt_reinit(",
            "void furi_hal_bt_set_key_storage_change_callback(",
        ))
        run_c(INCLUDES + r'''
#define FURI_LOG_I(...) ((void)0)
#define furi_check assert
#define HSEM 0
#define CFG_HW_CLK48_CONFIG_SEMID 0
typedef int FuriMutex;
typedef int FuriHalBtStack;
typedef void(*BleGlueKeyStorageChangedCallback)(uint8_t*,uint16_t,void*);
typedef struct Profile Profile;
typedef struct {void(*stop)(Profile*);} Config;
struct Profile {const Config* config;};
static Profile* current_profile;
enum {FuriHalBusHSEM,FuriHalBusIPCC,FuriHalBusAES2,FuriHalBusPKA,FuriHalBusCRC,
      FuriMutexTypeNormal,FuriHalBtStackUnknown};

static BleGlueKeyStorageChangedCallback listener,expected;
static void* listener_context;
static unsigned glue_generation,restores,stack_starts,received,insomnia,locked,allocations,stops;
static bool glue_ready;
static uint8_t mock_event[2];
void furi_hal_bus_enable(int bus){(void)bus;}
void furi_hal_bus_disable(int bus){(void)bus;}
FuriMutex* furi_mutex_alloc(int type){assert(type==FuriMutexTypeNormal);allocations++;static FuriMutex m;return &m;}
int LL_HSEM_1StepLock(int h,int s){(void)h;(void)s;return 0;}
void ble_glue_init(void){assert(!glue_ready);glue_ready=true;listener=NULL;listener_context=NULL;glue_generation++;}
void ble_glue_set_key_storage_changed_callback(BleGlueKeyStorageChangedCallback cb,void* context){
    assert(glue_ready&&cb);
    listener=cb;listener_context=context;restores++;
}
void furi_hal_bt_lock_core2(void){assert(!locked);locked=1;}
void furi_hal_bt_unlock_core2(void){assert(locked);locked=0;}
void furi_hal_power_insomnia_enter(void){insomnia++;}
void furi_hal_power_insomnia_exit(void){assert(insomnia);insomnia--;}
void furi_hal_bt_stop_advertising(void){}
void hci_reset(void){}
void gap_thread_stop(void){}
void ble_app_deinit(void){}
bool ble_glue_reinit_c2(void){assert(glue_ready);return true;}
void ble_glue_stop(void){assert(glue_ready);glue_ready=false;listener=NULL;listener_context=NULL;}
void furi_delay_ms(uint32_t delay){assert(delay==100);}
bool furi_hal_bt_start_radio_stack(void){
    assert(glue_ready&&!locked);
    stack_starts++;
    assert(listener==expected);
    if(listener)listener(mock_event,sizeof(mock_event),listener_context);
    return true;
}
''' + state + functions + r'''
static void stop(Profile* p){assert(p==current_profile);stops++;}
static void callback(uint8_t* event,uint16_t size,void* context){
    assert(event==mock_event&&size==2&&context);(*(unsigned*)context)++;received++;
}
int main(void){
    furi_hal_bt_init();assert(!listener&&!restores);
    furi_hal_bt_reinit();assert(!listener&&stack_starts==1&&glue_generation==2);
    unsigned first=0,second=0;
    furi_hal_bt_set_key_storage_change_callback(callback,&first);
    expected=callback;
    Config cfg={.stop=stop};Profile profile={.config=&cfg};current_profile=&profile;
    furi_hal_bt_reinit();
    assert(first==1&&received==1&&stops==1&&!current_profile&&!insomnia);
    furi_hal_bt_set_key_storage_change_callback(callback,&second);
    furi_hal_bt_reinit();furi_hal_bt_reinit();
    assert(first==1&&second==2&&received==3&&restores==5&&glue_generation==5&&allocations==1);
    return 0;
}
''')

    def test_boot_registers_before_keys_and_settings_can_restart_radio(self):
        source = BT.read_text()
        startup = re.search(
            r"if\(furi_hal_bt_start_radio_stack\(\)\) \{.*?\n    \}",
            source[source.index("int32_t bt_srv("):], re.S).group()
        run_c(INCLUDES + r'''
typedef int Bt;
static bool running;
static unsigned order,registered,loaded;
bool furi_hal_bt_start_radio_stack(void){return running;}
void bt_on_key_storage_change_callback(void){}
void furi_hal_bt_set_key_storage_change_callback(void(*f)(void),Bt* context){
    assert(f&&context&&order==0);order=1;registered++;
}
void bt_init_keys_settings(Bt* context){assert(context&&order==1);order=2;loaded++;}
static void boot(Bt* bt){
''' + startup + r'''
}
int main(void){
    Bt bt=0;boot(&bt);assert(!registered&&!loaded);
    running=true;boot(&bt);assert(registered==1&&loaded==1&&order==2);return 0;
}
''')

    def test_link_report_lists_each_status_without_opening_or_modifying_files(self):
        run_c(INCLUDES + r'''
typedef int Storage;
typedef enum {FSE_OK,FSE_NOT_READY,FSE_NOT_EXIST,FSE_INTERNAL} FS_Error;
typedef struct {bool directory;uint64_t size;} FileInfo;
typedef char FuriString[1024];
static bool sd=true,remove_mid_scan=false,remove_after_scan=false;
static unsigned stats,status_reads;
FS_Error storage_sd_status(Storage* storage){assert(storage);status_reads++;return sd?FSE_OK:FSE_NOT_READY;}
FS_Error storage_common_stat(Storage* storage,const char* path,FileInfo* meta){
    assert(storage&&sd&&!strncmp(path,"/ext/",5)&&meta);stats++;
    if(remove_mid_scan&&stats==2){sd=false;return FSE_NOT_READY;}
    if(strstr(path,"missing"))return FSE_NOT_EXIST;
    if(strstr(path,"error"))return FSE_INTERNAL;
    *meta=(FileInfo){.directory=strstr(path,"folder")!=NULL,.size=strstr(path,"empty")?0:123};
    if(remove_after_scan&&stats==8)sd=false;
    return FSE_OK;
}
bool file_info_is_dir(const FileInfo* meta){return meta->directory;}
void furi_string_set(FuriString* text,const char* literal){assert(strlen(literal)<sizeof(*text));strcpy(*text,literal);}
void furi_string_printf(FuriString* text,const char* fmt,...){
    va_list a;va_start(a,fmt);assert(vsnprintf(*text,sizeof(*text),fmt,a)>=0);va_end(a);
}
void furi_string_cat_printf(FuriString* text,const char* fmt,...){
    size_t n=strlen(*text);va_list a;va_start(a,fmt);
    assert(vsnprintf(*text+n,sizeof(*text)-n,fmt,a)<(int)(sizeof(*text)-n));va_end(a);
}
void furi_string_cat(FuriString* text,const char* more){
    assert(strlen(*text)+strlen(more)<sizeof(*text));strcat(*text,more);
}
''' + app_source("library_model.h") + app_source("library_model.c")
              + app_source("field_links.h") + app_source("field_links.c") + r'''
void field_links_report(Storage*,const DeviceCard*,FuriString*);
int main(void){
    Storage storage=0;DeviceCard c={.name="Workshop"};
    FuriString text={0};field_links_report(&storage,&c,&text);
    assert(strstr(text,"No linked files")&&!stats&&!status_reads);
    const char* names[]={ "present.sub", "missing.sub", "empty.ir", "folder", "error.nfc",
                         "r6.txt", "r7.txt", "r8.txt" };
    c.link_count=8;
    for(unsigned i=0;i<c.link_count;i++)snprintf(c.links[i],sizeof(c.links[i]),"/ext/test/%s",names[i]);
    strcpy(c.labels[0],"Lamp");
    DeviceCard before=c;
    field_links_report(&storage,&c,&text);
    assert(stats==8&&!memcmp(&c,&before,sizeof(c)));
    const char* first_screen="Linked files\nWorkshop\n4/8 present\n1. Present\nLamp\n";
    assert(!strncmp(text,first_screen,strlen(first_screen))&&strstr(text,"Missing\nmissing.sub"));
    assert(strstr(text,"Empty\nempty.ir")&&strstr(text,"Folder\nfolder")&&strstr(text,"Not checked\nerror.nfc"));
    assert(strstr(text,"Contents not checked")&&strstr(text,"Round unchanged"));
    sd=false;stats=0;field_links_report(&storage,&c,&text);
    assert(!stats&&strstr(text,"SD unavailable")&&!strstr(text,"Missing"));
    sd=true;remove_mid_scan=true;field_links_report(&storage,&c,&text);
    assert(stats==2&&strstr(text,"SD unavailable")&&!strstr(text,"present"));
    stats=0;sd=true;remove_mid_scan=false;remove_after_scan=true;
    field_links_report(&storage,&c,&text);
    assert(stats==8&&strstr(text,"SD unavailable")&&!strstr(text,"present"));
    c.link_count=9;stats=0;field_links_report(&storage,&c,&text);assert(!stats&&strstr(text,"Invalid object"));
    field_links_report(NULL,&c,&text);assert(!stats&&strstr(text,"SD unavailable"));
    field_links_report(&storage,&c,NULL);assert(!stats);
    return 0;
}
''')

    def test_check_files_is_available_only_for_a_valid_selected_round_item(self):
        round_source = app_source("field_round.c")
        helpers = function(app_source("library_model.c"), "bool library_name_valid(")
        helpers += function(app_source("field_io.c"), "bool field_text_valid(")
        for signature in ("static bool field_round_valid(", "bool field_round_pending_count(",
                          "bool field_round_next_pending("):
            helpers += function(round_source, signature)
        run_c(INCLUDES + "\ntypedef int Storage;\n"
              + app_source("library_model.h") + app_source("field_round.h") + helpers + r'''
typedef struct {unsigned action_count;} Submenu;
static unsigned action=0;
void submenu_reset(Submenu* menu){menu->action_count=0;}
void submenu_set_header(Submenu* menu,const char* text){assert(menu&&text);}
void submenu_add_item(Submenu* menu,const char* text,uint32_t id,void(*cb)(void*,uint32_t),void* context){
    (void)cb;(void)context;
    if(!strcmp(text,"Check linked files")){action=id;menu->action_count++;}
}
const char* field_round_result_name(FieldRoundResult result){(void)result;return "Pending";}
''' + app_source("field_menu.h") + app_source("field_menu.c") + r'''
int main(void){
    FieldRound round={.count=1,.items={{.object="Workshop"}}};
    FieldMenuState s={.round=&round,.page=FieldPageItem};Submenu menu;
    field_menu_build(&menu,&s,NULL,NULL);
    assert(action&&menu.action_count==1&&field_menu_allows(&s,action));
    assert(field_menu_allows(&s,FieldOpenObject)&&field_menu_allows(&s,FieldPass));
    s.selected=1;
    field_menu_build(&menu,&s,NULL,NULL);
    assert(menu.action_count==0&&!field_menu_allows(&s,action));
    s.selected=0;round.count=99;
    field_menu_build(&menu,&s,NULL,NULL);
    assert(menu.action_count==0&&!field_menu_allows(&s,action));
    round.count=1;
    s.round=NULL;assert(!field_menu_allows(&s,action));s.round=&round;
    for(unsigned p=FieldPageHome;p<=FieldPageInput;p++){
        if(p==FieldPageItem)continue;
        s.page=(FieldPage)p;assert(!field_menu_allows(&s,action));
    }
    return 0;
}
''')

    def test_check_uses_selected_card_and_back_preserves_round(self):
        source = (APP / "field_tools.c").read_text()
        event = function(source, "static bool field_tools_event(")
        self.assertRegex(
            event, r"else if\(event == FieldCheckLinkedFiles\)\s*\{\s*field_tools_check_links\(app\);"
        )
        self.assertLess(event.index("if(!field_menu_allows("), event.index("event == FieldCheckLinkedFiles"))
        run_c(INCLUDES + "\ntypedef int Storage;\n"
              + app_source("library_model.h") + app_source("field_round.h") + r'''
typedef void Submenu;
''' + app_source("field_menu.h") + r'''
typedef char FuriString[128];
typedef struct {
    Storage* storage;
    void *text,*thread,*dispatcher;
    FuriString* message;
    FieldMenuState state;
    FieldPage back;
    FieldRound round;
    DeviceCard card;
} FieldTools;
enum {FieldTextView=1};
static unsigned reads,reports,views;
static bool card_available=true;
static const char* displayed;
static void field_tools_show(FieldTools* app,FieldPage page){app->state.page=page;}
static bool library_card_load(Storage* storage,const char* name,DeviceCard* c){
    assert(storage&&!strcmp(name,"Workshop"));reads++;
    if(!card_available)return false;
    strcpy(c->name,name);return true;
}
static void field_links_report(Storage* storage,const DeviceCard* c,FuriString* m){
    assert(storage&&!strcmp(c->name,"Workshop"));reports++;strcpy(*m,"New report");
}
static const char* furi_string_get_cstr(const FuriString* s){return *s;}
static void furi_string_set(FuriString* s,const char* text){assert(text!=*s);strcpy(*s,text);}
static void text_box_reset(void* text){assert(text);displayed=NULL;}
static void text_box_set_text(void* text,const char* value){assert(text&&!displayed);displayed=value;}
static void view_dispatcher_switch_to_view(void* dispatcher,unsigned view){
    assert(dispatcher&&view==FieldTextView);views++;
}
static void view_dispatcher_stop(void* dispatcher){(void)dispatcher;assert(false);}
static void* furi_thread_get_id(void* thread){(void)thread;return NULL;}
static void furi_thread_flags_set(void* thread,unsigned flags){(void)thread;(void)flags;assert(false);}
''' + "\n".join(function(source, sig) for sig in (
                  "static void field_tools_message(", "static void field_tools_check_links(",
                  "static bool field_tools_back(",
              )) + r'''
int main(void){
    Storage storage=0;FuriString message={"Stale report"};
    FieldTools app={.storage=&storage,.text=&storage,.dispatcher=&storage,.message=&message,
        .state={.page=FieldPageItem,.selected=1},
        .round={.count=2,.items={{.object="Other"},{.object="Workshop",.result=FieldRoundProblem}}}};
    app.state.round=&app.round;
    const FieldRound original=app.round;
    field_tools_check_links(&app);
    assert(reads==1&&reports==1&&views==1&&!strcmp(displayed,"New report"));
    assert(app.state.page==FieldPageMessage&&field_tools_back(&app));
    assert(app.state.page==FieldPageItem&&app.state.selected==1);
    card_available=false;
    field_tools_check_links(&app);
    assert(reads==2&&reports==1&&views==2&&strstr(displayed,"No files checked"));
    assert(!strstr(displayed,"New report"));
    assert(app.state.page==FieldPageMessage&&field_tools_back(&app));
    assert(app.state.page==FieldPageItem&&app.state.selected==1);
    assert(!memcmp(&app.round,&original,sizeof(original)));
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
