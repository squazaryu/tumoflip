"""Production wallet menu constructors, without GUI input or radio hardware."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class WalletMenuTests(unittest.TestCase):
    def test_back_keeps_edit_context_and_never_discards_dirty_objects(self):
        source = (APP / "device_library.c").read_text()
        enums = source[source.index("enum {"):source.index("typedef struct {")]
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
''' + enums + r'''
typedef struct{LibraryPage page,back;unsigned edit;bool dirty;void*dispatcher;}Library;
static unsigned stops;
static void view_dispatcher_stop(void*d){(void)d;stops++;}
static void library_show(Library*a,LibraryPage p){a->page=p;}
''' + function(source, "static bool library_back(") + r'''
int main(void){
 Library a={.page=PageManage,.dirty=true};library_back(&a);assert(a.page==PageCard&&a.dirty);
 library_back(&a);assert(a.page==PageDiscard&&a.dirty);library_back(&a);assert(a.page==PageCard&&a.dirty);
 a.page=PageLink;library_back(&a);assert(a.page==PageManage);
 a.page=PageInput;a.edit=RenameAction;library_back(&a);assert(a.page==PageLink);
 a.page=PageInput;a.edit=Note;library_back(&a);assert(a.page==PageManage);
 a.page=PageInput;a.edit=NewCard;library_back(&a);assert(a.page==PageMain);
 a.page=PageConfirmUnpin;library_back(&a);assert(a.page==PagePins);
 a.page=PageRemovePins;library_back(&a);assert(a.page==PagePins);
 a.page=PageText;a.back=PageManage;library_back(&a);assert(a.page==PageManage);
 a.page=PageMain;library_back(&a);assert(stops==1);
 return 0;
}
''')

    def test_open_uses_explicit_native_routes_and_blocks_unsafe_or_dirty_launches(self):
        source = (APP / "device_library.c").read_text()
        model = "\n".join(re.sub(r"^#(?:include|pragma).*\n", "", (APP / name).read_text(), flags=re.M)
                          for name in ("library_model.h", "library_model.c"))
        enums = source[source.index("enum {"):source.index("typedef struct {")]
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
''' + model + enums + r'''
typedef struct{char text[300];}FuriString;
typedef struct{LibraryPage page;DeviceCard card;unsigned link;bool dirty;void*storage,*loader,*dispatcher;}Library;
enum{LoaderDeferredLaunchFlagGui};
static bool exists=true,ready=true;static unsigned cleared,queued,stopped,messages;static LibraryPage back;
static char app_name[40],argument[300];
static void library_message(Library*a,const char*m,LibraryPage p){(void)a;assert(m&&*m);messages++;back=p;}
static bool storage_file_exists(void*s,const char*p){(void)s;assert(*p);return exists;}
static bool library_native_inspect_ready(void*s,LibraryFileKind k){(void)s;assert(k!=LibraryFileReference);return ready;}
static FuriString*furi_string_alloc_printf(const char*f,...){FuriString*s=malloc(sizeof(*s));va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);return s;}
static const char*furi_string_get_cstr(FuriString*s){return s->text;}
static void furi_string_free(FuriString*s){free(s);}
static void loader_clear_launch_queue(void*l){(void)l;assert(!queued);cleared++;}
static void loader_enqueue_launch(void*l,const char*n,const char*p,int f){(void)l;(void)f;assert(cleared&&!stopped);strcpy(app_name,n);strcpy(argument,p);queued++;}
static void view_dispatcher_stop(void*d){(void)d;assert(queued);stopped++;}
static void reset(void){cleared=queued=stopped=messages=0;exists=ready=true;argument[0]=app_name[0]=0;}
''' + function(source, "static void library_open_link(") + r'''
int main(void){
 Library a={.page=PageCard,.card={.name="Office",.link_count=1}};
 const char*paths[]={"/ext/a.nfc","/ext/a.rfid","/ext/a.ibtn","/ext/a.sub","/ext/a.IR"};
 const char*targets[]={"NFC","125 kHz RFID","iButton","Sub-GHz","Infrared"};
 for(unsigned i=0;i<5;i++){reset();strcpy(a.card.links[0],paths[i]);library_open_link(&a);assert(queued==1&&stopped==1&&!strcmp(app_name,targets[i]));
 char expected[300];snprintf(expected,sizeof(expected),"%s%s",i<3?"inspect:":"",paths[i]);assert(!strcmp(argument,expected));}
 const char*blocked[]={"/ext/x.js","/ext/x.fap","/ext/../a.nfc"};
 for(unsigned i=0;i<3;i++){reset();strcpy(a.card.links[0],blocked[i]);library_open_link(&a);assert(!queued&&!cleared&&messages==1&&back==PageCard);}
 strcpy(a.card.links[0],paths[0]);reset();a.dirty=true;library_open_link(&a);assert(!queued&&messages==1);a.dirty=false;
 reset();exists=false;library_open_link(&a);assert(!queued&&messages==1);
 reset();ready=false;library_open_link(&a);assert(!queued&&messages==1);
 reset();a.link=1;library_open_link(&a);assert(!queued&&!messages);
 return 0;
}
''')

    def test_object_actions_are_first_and_use_the_saved_labels(self):
        source = (APP / "device_library.c").read_text()
        model = "\n".join(re.sub(r"^#(?:include|pragma).*\n", "", (APP / name).read_text(), flags=re.M)
                          for name in ("library_model.h", "library_model.c"))
        enums = source[source.index("enum {"):source.index("typedef struct {")]
        pinned = function(source, "static void library_pinned(") if "static void library_pinned(" in source else "static void __attribute__((unused)) library_pinned(Library*a,bool b){(void)a;(void)b;}"
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
''' + model + enums + r'''
typedef struct {char labels[40][256];unsigned values[40],count;char header[40];}Submenu;
typedef struct{unsigned generation,size;}FileHistoryRecord;
typedef struct{bool(*list)(void*,const char*,FileHistoryRecord*);}HistoryApi;
typedef const char FuriString;
typedef struct{Submenu*menu;DeviceCard card;LibraryPins pins;bool dirty,pins_ready;unsigned card_offset,count,link,pin_edit;void*storage,*dispatcher;FuriString*path;HistoryApi*history;FileHistoryRecord records[4];int page;}Library;
enum{FSE_OK,FSE_NOT_EXIST};typedef int FS_Error;
#define FILE_HISTORY_ENABLED "flag"
static const char*furi_string_get_cstr(FuriString*p){return p;}
static void submenu_reset(Submenu*m){m->count=0;}
static void submenu_set_header(Submenu*m,const char*s){assert(strlen(s)<sizeof(m->header));strcpy(m->header,s);}
static void library_item(Library*a,const char*s,unsigned id){assert(a->menu->count<40);strcpy(a->menu->labels[a->menu->count],s);a->menu->values[a->menu->count++]=id;}
static void library_cards(Library*a){library_item(a,"Garage",CardBase);}
static bool library_history_load(Library*a){(void)a;return false;}
static bool file_history_core_available(void){return false;}
static int storage_common_stat(void*s,const char*p,void*i){(void)s;(void)p;(void)i;return FSE_NOT_EXIST;}
static void view_dispatcher_switch_to_view(void*d,unsigned id){(void)d;(void)id;}
''' + pinned + "\n" + function(source, "static void library_show(Library* app, LibraryPage page) {").replace("%lu", "%u") + r'''
int main(void){
 Submenu m={0};Library a={.menu=&m,.card={.name="Garage",.link_count=2}};
 strcpy(a.card.links[0],"/ext/nfc/Pass.nfc");strcpy(a.card.links[1],"/ext/lfrfid/Entry.rfid");strcpy(a.card.labels[0],"Pass");strcpy(a.card.labels[1],"Entry");
 library_show(&a,PageCard);assert(m.count>=4);assert(!strcmp(m.labels[1],"Pass")&&m.values[1]==LinkBase);assert(!strcmp(m.labels[2],"Entry")&&m.values[2]==LinkBase+1);
 a.card.labels[0][0]=0;library_show(&a,PageCard);assert(!strcmp(m.labels[1],"Pass.nfc"));
 a.dirty=true;library_show(&a,PageCard);bool save=false;for(unsigned i=0;i<m.count;i++)if(m.values[i]==SaveCard)save=true;assert(save);
 library_show(&a,PageLink);assert(m.count>=3);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
