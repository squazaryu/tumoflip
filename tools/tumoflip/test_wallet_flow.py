"""Real wallet event loop and on-disk records, with GUI/loader boundary stubs."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_field_wallet import storage_native

ROOT = Path(__file__).resolve().parents[2]


class WalletFlowTests(unittest.TestCase):
    def test_create_label_pin_open_and_remove_pin_without_touching_capture(self):
        source = (ROOT / "applications/system/device_library/device_library.c").read_text()
        ui = re.sub(r"^#include.*\n", "", source.split("int32_t device_library_app(", 1)[0], flags=re.M)
        history = re.sub(r"^#(?:include|pragma).*\n", "", (ROOT / "lib/toolbox/file_history.h").read_text(), flags=re.M)
        storage_native(r'''
typedef int DialogsApp,Loader,Gui,ViewDispatcher,TextBox,TextInput;
typedef struct{unsigned count;}Submenu;
typedef struct{const char*base_path;}DialogsFileBrowserOptions;
typedef struct{unsigned year,month,day,hour,minute;}DateTime;
enum{LoaderDeferredLaunchFlagGui};
static unsigned launches,stops;static const char*selected_path="/ext/nfc/Pass.nfc";
static char launched_app[40],launched_path[300];
static void view_dispatcher_send_custom_event(void*v,uint32_t e){(void)v;(void)e;}
static void view_dispatcher_switch_to_view(void*v,unsigned n){(void)v;(void)n;}
static void view_dispatcher_show_loading(void*v){(void)v;}
static void view_dispatcher_stop(void*v){(void)v;stops++;}
static void submenu_reset(Submenu*m){m->count=0;}
static void submenu_set_header(Submenu*m,const char*s){(void)m;assert(s&&*s);}
static void submenu_add_item(Submenu*m,const char*s,unsigned id,void(*cb)(void*,uint32_t),void*c){(void)id;(void)cb;(void)c;assert(s&&*s);m->count++;}
static void text_box_reset(TextBox*t){(void)t;}
static void text_box_set_text(TextBox*t,const char*s){(void)t;assert(s);}
static void text_input_reset(TextInput*t){(void)t;}
static void text_input_set_minimum_length(TextInput*t,unsigned n){(void)t;(void)n;}
static void text_input_set_header_text(TextInput*t,const char*s){(void)t;assert(s);}
static void text_input_set_result_callback(TextInput*t,void(*cb)(void*),void*c,char*b,size_t n,bool clear){(void)t;(void)cb;(void)c;(void)clear;assert(b&&n<=128);}
static void dialog_file_browser_set_basic_options(DialogsFileBrowserOptions*o,const char*e,void*i){(void)o;(void)e;(void)i;}
static bool dialog_file_browser_show(DialogsApp*d,FuriString*p,FuriString*q,DialogsFileBrowserOptions*o){(void)d;(void)q;(void)o;furi_string_set(p,selected_path);return true;}
static bool storage_file_exists(Storage*s,const char*p){return storage_common_stat(s,p,NULL)==FSE_OK;}
static void loader_clear_launch_queue(Loader*l){(void)l;}
static bool loader_get_application_launch_path(Loader*l,FuriString*p){(void)l;(void)p;return false;}
static void furi_string_reset(FuriString*s){furi_string_set(s,"");}
static void loader_enqueue_launch(Loader*l,const char*a,const char*p,int f){
 (void)l;(void)f;size_t na=strlen(a),np=strlen(p);
 assert(na<sizeof(launched_app)&&np<sizeof(launched_path));
 memcpy(launched_app,a,na+1);memcpy(launched_path,p,np+1);launches++;
}
static void furi_hal_rtc_get_datetime(DateTime*d){*d=(DateTime){2026,10,24,23,0};}
''' + history + ui.replace("%lu", "%u") + r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/nfc"));
 File*f=storage_file_alloc(&s);const char original[]="Original capture stays byte-identical";
 assert(storage_file_open(f,selected_path,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,original,sizeof(original))==sizeof(original));assert(storage_file_close(f));storage_file_free(f);
 Submenu menu={0};Library a={.storage=&s,.menu=&menu,.text=furi_string_alloc(),.path=furi_string_alloc()};
 library_show(&a,PageMain);library_event(&a,NewCard);strcpy(a.edit_buffer,"Office");library_event(&a,NameDone);assert(a.dirty&&a.page==PageCard);
 library_event(&a,AddLink);assert(a.card.link_count==1&&a.dirty);library_event(&a,SaveCard);assert(!a.dirty);library_back(&a);
 library_event(&a,EditObject);library_event(&a,LinkBase);assert(a.page==PageLink);
 library_event(&a,RenameAction);strcpy(a.edit_buffer,"Entry pass");library_event(&a,NameDone);assert(a.dirty&&!strcmp(a.card.labels[0],"Entry pass"));
 library_back(&a);assert(a.page==PageManage);library_event(&a,SaveCard);library_back(&a);library_event(&a,EditObject);
 library_event(&a,TogglePin);assert(a.pins.count==1&&library_pins_contains(&a.pins,"Office"));
 library_back(&a);library_event(&a,LinkBase);assert(launches==1&&stops==1&&!strcmp(launched_app,"NFC")&&!strcmp(launched_path,"inspect:/ext/nfc/Pass.nfc"));
 library_show(&a,PageMain);library_event(&a,OpenPins);library_event(&a,RemovePins);library_event(&a,PinRemoveBase);assert(a.page==PageConfirmUnpin);
 // A refreshed confirmation may see a pin already removed by another writer.
 // Confirming removal must never turn that pin back on.
 LibraryPins external={0};assert(library_pins_save(&s,&external));library_event(&a,ConfirmUnpin);
 assert(a.page==PagePins&&!library_pins_contains(&a.pins,"Office"));assert(library_pins_load(&s,&external)&&!external.count);
 assert(library_card_load(&s,"Office",&a.card)&&!strcmp(a.card.labels[0],"Entry pass"));
 // A per-object Sensor Watch launch carries only the validated object name.
 assert(storage_simply_mkdir(&s,"/ext/apps"));assert(storage_simply_mkdir(&s,"/ext/apps/Sub-GHz"));
 f=storage_file_alloc(&s);assert(storage_file_open(f,"/ext/apps/Sub-GHz/sensor_watch.fap",FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_close(f));storage_file_free(f);
 library_show(&a,PageCard);unsigned before=launches;library_event(&a,OpenSensorWatch);
 assert(launches==before+1&&!strcmp(launched_app,"/ext/apps/Sub-GHz/sensor_watch.fap")&&!strcmp(launched_path,"object:Office"));
 char bytes[sizeof(original)];f=storage_file_alloc(&s);assert(storage_file_open(f,selected_path,FSAM_READ,FSOM_OPEN_EXISTING));assert(storage_file_read(f,bytes,sizeof(bytes))==sizeof(bytes)&&!memcmp(bytes,original,sizeof(bytes)));assert(storage_file_close(f));storage_file_free(f);
 furi_string_free(a.text);furi_string_free(a.path);free(a.sources);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
