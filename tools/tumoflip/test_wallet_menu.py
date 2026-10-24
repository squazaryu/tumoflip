"""Production wallet menu constructors, without GUI input or radio hardware."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class WalletMenuTests(unittest.TestCase):
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
