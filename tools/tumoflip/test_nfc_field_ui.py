"""Production Back behavior and save readback on a disposable native fixture."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_field_wallet import storage_native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nfc_field_sniffer/nfc_field_sniffer.c"


class NfcFieldUiTests(unittest.TestCase):
    def test_back_cancels_busy_returns_from_text_and_exits_home(self):
        source = APP.read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct{void*worker,*dispatcher;bool showing_text;}NfcFieldApp;
static unsigned stopped,cancelled,menus;
static void*furi_thread_get_id(void*w){return w;}
static void furi_thread_flags_set(void*w,unsigned n){assert(w&&n==1);cancelled++;}
void view_dispatcher_stop(void*d){(void)d;stopped++;}
static void nfc_field_menu(NfcFieldApp*a){a->showing_text=false;menus++;}
''' + function(source, "static bool nfc_field_back(") + r'''
int main(void){NfcFieldApp a={0};nfc_field_back(&a);assert(stopped==1&&!menus&&!cancelled);
 a.showing_text=true;nfc_field_back(&a);assert(stopped==1&&menus==1&&!a.showing_text);
 a.worker=&a;nfc_field_back(&a);assert(cancelled==1&&stopped==1);return 0;}
''')

    def test_trace_save_is_create_new_verified_and_preserves_old_files(self):
        source = APP.read_text()
        storage_native(r'''
typedef struct{Storage*storage;bool ready;char trace[4096];}NfcFieldApp;
''' + function(source, "static bool nfc_field_save(") + r'''
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 NfcFieldApp a={.storage=&s,.ready=true,.trace="Own passive trace\nR 0us 7b: 26\n"};char first[128],next[128];
 assert(nfc_field_save(&a,first));assert(nfc_field_save(&a,next)&&strcmp(first,next));
 write_budget=1;assert(!nfc_field_save(&a,next));write_budget=-1;
 sync_failure=true;assert(!nfc_field_save(&a,next));sync_failure=false;
 close_failure=true;assert(!nfc_field_save(&a,next));close_failure=false;
 assert(storage_common_stat(&s,first,NULL)==FSE_OK);a.ready=false;assert(!nfc_field_save(&a,next));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
