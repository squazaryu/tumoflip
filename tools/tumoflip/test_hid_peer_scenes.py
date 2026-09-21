"""Execute the actual device-picker scenes with BT/storage failure fixtures."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native as run_c
from tools.tumoflip.test_hid_peer_store import stripped

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/hid_app"


SUPPORT = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define HID_TRANSPORT_BLE 1
#define GAP_BONDED_DEVICES_MAX 35
#define RECORD_STORAGE "storage"
typedef int Storage;
typedef struct{uint8_t address_type,address[6];}GapBondedDevice;
typedef struct{uint8_t count;GapBondedDevice devices[35];}GapBondedDevices;
''' + stripped(APP / "helpers/hid_peer_store.h") + r'''
enum{HidViewDialog,HidViewSubmenu,HidViewTextInput,HidScenePeerName,HidSceneStart};
enum{HidPeerConnected=0x7000,HidPeerDisconnected,HidPeerNameDone,HidPeerDialogBack,HidPeerDialogConfirm};
enum{SceneManagerEventTypeBack,SceneManagerEventTypeCustom,AlignCenter,AlignTop,DialogExResultRight};
typedef int DialogExResult;
typedef struct{int type;uint32_t event;}SceneManagerEvent;
typedef struct{void*bt,*dialog,*submenu,*view_dispatcher,*scene_manager,*text_input,*ble_hid_profile;int ble_hid_cfg;
 HidPeerStore peer_store;GapBondedDevices peer_list;
 bool peer_dialog,peer_pairing_dialog,peer_confirm_forget,peer_connected,peer_active,peer_connect_after_name;
 GapBondedDevice peer_name_target;
 char peer_name[13],peer_header[32];}Hid;
static void*ble_profile_hid_ext;
static int saves,selects,disconnects,previous,next_scene,view,rows,forgets,starts;
static char row_labels[50][24];
static bool save_ok=true,select_ok=true,read_ok=true,forget_ok=true,start_ok=true,open_pairing;
static GapBondedDevice target;
static void(*text_cb)(void*);
static void(*dialog_cb)(DialogExResult,void*);
static void(*menu_cb)(void*,uint32_t);
static uint32_t queued_event;
static void*furi_record_open(const char*n){(void)n;return NULL;}
static void furi_record_close(const char*n){(void)n;}
bool hid_peer_store_save(Storage*s,HidPeerStore*store,const HidPeerPreferences*p){(void)s;saves++;if(save_ok)store->preferences=*p;return save_ok;}
static void bt_disconnect(void*b){(void)b;disconnects++;}
static bool bt_set_connection_peer(void*b,const GapBondedDevice*p){(void)b;selects++;open_pairing=!p;if(p)target=*p;return select_ok;}
static bool bt_forget_bonded_device(void*b,const GapBondedDevice*p){(void)b;assert(p);forgets++;return forget_ok;}
static bool bt_get_bonded_devices(void*b,GapBondedDevices*d){(void)b;memset(d,0,sizeof(*d));if(read_ok){d->count=2;d->devices[0].address[0]=1;d->devices[1].address[0]=2;}return read_ok;}
static void*bt_profile_start_idle(void*b,void*t,void*p){(void)b;(void)t;(void)p;starts++;return start_ok?(void*)1:NULL;}
static void submenu_reset(void*v){(void)v;rows=0;memset(row_labels,0,sizeof(row_labels));}
static void submenu_set_header(void*v,const char*t){(void)v;assert(strlen(t)<24);}
static void submenu_add_item(void*v,const char*t,uint32_t n,void(*cb)(void*,uint32_t),void*c){(void)v;(void)n;menu_cb=cb;(void)c;assert(strlen(t)<24);strcpy(row_labels[rows++],t);}
static void submenu_set_selected_item(void*v,uint32_t n){(void)v;(void)n;}
static void dialog_ex_reset(void*v){(void)v;}
static void dialog_ex_set_context(void*v,void*c){(void)v;(void)c;}
static void dialog_ex_set_result_callback(void*v,void(*cb)(DialogExResult,void*)){(void)v;dialog_cb=cb;}
static void dialog_ex_set_header(void*v,const char*t,int x,int y,int a,int b){(void)v;(void)t;(void)x;(void)y;(void)a;(void)b;}
static void dialog_ex_set_text(void*v,const char*t,int x,int y,int a,int b){(void)v;(void)t;(void)x;(void)y;(void)a;(void)b;}
static void dialog_ex_set_left_button_text(void*v,const char*t){(void)v;(void)t;}
static void dialog_ex_set_right_button_text(void*v,const char*t){(void)v;(void)t;}
static void view_dispatcher_switch_to_view(void*v,int n){(void)v;view=n;}
static void view_dispatcher_send_custom_event(void*v,uint32_t n){(void)v;queued_event=n;}
static void scene_manager_previous_scene(void*v){(void)v;previous++;}
static void scene_manager_next_scene(void*v,int n){(void)v;next_scene=n;}
__attribute__((unused)) static bool scene_manager_search_and_switch_to_previous_scene(void*v,int n){(void)v;next_scene=n;previous++;return true;}
static void text_input_reset(void*v){(void)v;}
static void text_input_set_header_text(void*v,const char*t){(void)v;(void)t;}
static void text_input_set_result_callback(void*v,void(*cb)(void*),void*c,char*t,size_t n,bool b){(void)v;text_cb=cb;(void)c;(void)t;assert(n==13);(void)b;}
''' + stripped(APP / "scenes/hid_scene_devices.c") + "\n" + stripped(APP / "scenes/hid_scene_peer_name.c") + r'''
static void event(Hid*a,uint32_t id){assert(hid_scene_devices_on_event(a,(SceneManagerEvent){SceneManagerEventTypeCustom,id}));}
'''


class HidPeerScenesTests(unittest.TestCase):
    def test_friendly_labels_are_read_only_and_do_not_collide_with_saved_names(self):
        run_c(SUPPORT + r'''
int main(void){
 Hid a={.ble_hid_profile=(void*)1};hid_scene_devices_on_enter(&a);
 assert(!strcmp(row_labels[0],"Device 1")&&!strcmp(row_labels[1],"Device 2")&&!saves);
 a.peer_store.preferences.count=1;a.peer_store.preferences.labels[0].peer=a.peer_list.devices[1];
 strcpy(a.peer_store.preferences.labels[0].name,"Device 1");hid_peer_devices_refresh(&a);
 assert(!strcmp(row_labels[0],"Device 2")&&!strcmp(row_labels[1],"Device 1")&&!saves);
 strcpy(a.peer_store.preferences.labels[0].name,"My MacBook");
 GapBondedDevice peer=a.peer_list.devices[1];a.peer_list.devices[1]=a.peer_list.devices[0];a.peer_list.devices[0]=peer;
 char label[13];hid_peer_label(&a,&peer,label,sizeof(label));assert(!strcmp(label,"My MacBook"));
 GapBondedDevice missing={0,{99}};hid_peer_label(&a,&missing,label,sizeof(label));assert(!strcmp(label,"Saved device"));
 return 0;
}
''')

    def test_first_selection_names_then_connects_and_preserves_identity(self):
        run_c(SUPPORT + r'''
int main(void){
 Hid a={.ble_hid_profile=(void*)1};hid_scene_devices_on_enter(&a);
 event(&a,PeerFirst+1);
 assert(next_scene==HidScenePeerName && !selects && !saves && !a.peer_active);
 assert(a.peer_connect_after_name && a.peer_name_target.address[0]==2);
 hid_scene_peer_name_on_enter(&a);assert(!strcmp(a.peer_name,"Device 2"));
 strcpy(a.peer_name,"Office PC");
 assert(hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone}));
 assert(saves==1 && selects==1 && !open_pairing && target.address[0]==2 && a.peer_active);
 assert(next_scene==HidSceneStart && a.peer_store.preferences.peer.address[0]==2);
 assert(a.peer_store.preferences.count==1&&!strcmp(a.peer_store.preferences.labels[0].name,"Office PC"));
 hid_scene_peer_name_on_exit(&a);assert(!a.peer_connect_after_name);
 hid_scene_devices_on_enter(&a);assert(!strcmp(row_labels[1],"* Office PC"));
 event(&a,PeerFirst+1);assert(selects==2 && a.peer_active);
 return 0;
}
''')

    def test_cancel_and_invalid_names_never_connect_or_save(self):
        run_c(SUPPORT + r'''
int main(void){
 Hid a={.ble_hid_profile=(void*)1};hid_scene_devices_on_enter(&a);event(&a,PeerFirst);
 assert(!selects && !saves);
 hid_scene_peer_name_on_enter(&a);
 assert(!hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeBack,0}));
 hid_scene_peer_name_on_exit(&a);assert(!selects && !saves && !a.peer_connect_after_name);
 event(&a,PeerFirst);hid_scene_peer_name_on_enter(&a);
 const char* invalid[]={"","   ","bad\nname","bad\177name"};
 for(unsigned i=0;i<4;i++){
  strcpy(a.peer_name,invalid[i]);hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});
  assert(a.peer_dialog&&!selects&&!saves);
 }
 memset(a.peer_name,'X',sizeof(a.peer_name));
 hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});
 assert(!selects&&!saves);return 0;
}
''')

    def test_first_name_save_and_selection_fail_closed(self):
        run_c(SUPPORT + r'''
int main(void){
 Hid a={.ble_hid_profile=(void*)1};hid_scene_devices_on_enter(&a);event(&a,PeerFirst);
 hid_scene_peer_name_on_enter(&a);strcpy(a.peer_name,"Laptop");save_ok=false;
 hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});
 assert(a.peer_dialog&&!selects&&!a.peer_active&&!a.peer_store.preferences.selected);
 save_ok=true;select_ok=false;
 hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});
 assert(a.peer_dialog&&selects==1&&!a.peer_active&&!open_pairing);
 assert(a.peer_store.preferences.selected&&!strcmp(a.peer_store.preferences.labels[0].name,"Laptop"));
 hid_scene_peer_name_on_exit(&a);hid_scene_devices_on_enter(&a);select_ok=true;
 event(&a,PeerFirst);assert(a.peer_active&&selects==2&&!open_pairing);
 return 0;
}
''')

    def test_select_pair_cancel_forget_name_and_restart(self):
        run_c(SUPPORT + r'''
int main(void){
 Hid a={.ble_hid_profile=(void*)1};
 a.peer_store.preferences.count=2;
 a.peer_store.preferences.labels[0].peer.address[0]=1;strcpy(a.peer_store.preferences.labels[0].name,"Mac");
 a.peer_store.preferences.labels[1].peer.address[0]=2;strcpy(a.peer_store.preferences.labels[1].name,"PC");
 hid_scene_devices_on_enter(&a);assert(rows==4&&view==HidViewSubmenu);
 menu_cb(&a,PeerRefresh);assert(queued_event==PeerRefresh);
 event(&a,PeerFirst);assert(selects==1&&target.address[0]==1&&!open_pairing&&a.peer_active&&saves==1&&previous==1);
 hid_scene_devices_on_enter(&a);assert(rows==6);
 save_ok=false;event(&a,PeerFirst+1);assert(selects==1&&a.peer_dialog&&!a.peer_active);save_ok=true;
 int before=previous;event(&a,HidPeerDialogBack);assert(previous==before+1);
 select_ok=false;event(&a,PeerFirst+1);assert(a.peer_dialog&&!a.peer_active);select_ok=true;
 event(&a,HidPeerDialogBack);event(&a,PeerPair);assert(open_pairing&&a.peer_pairing_dialog);
 before=disconnects;assert(hid_scene_devices_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeBack,0}));
 assert(disconnects==before+1&&!a.peer_pairing_dialog&&view==HidViewSubmenu);
 event(&a,PeerRename);assert(next_scene==HidScenePeerName);hid_scene_peer_name_on_enter(&a);
 text_cb(&a);assert(queued_event==HidPeerNameDone);
 strcpy(a.peer_name,"Office PC");assert(hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone}));
 assert(a.peer_store.preferences.count==2&&!strcmp(a.peer_store.preferences.labels[1].name,"Office PC"));
 assert(!hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeBack,0}));
 assert(hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerConnected}));
 assert(!hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,42}));
 save_ok=false;hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});assert(a.peer_dialog);save_ok=true;
 dialog_cb(0,&a);assert(queued_event==HidPeerDialogBack);
 hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerDialogBack});
 HidPeerPreferences backup=a.peer_store.preferences;a.peer_store.preferences.count=35;
 for(unsigned i=0;i<35;i++)a.peer_store.preferences.labels[i].peer.address[0]=i+10;
 hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone});assert(a.peer_dialog);
 a.peer_store.preferences=backup;
 hid_scene_peer_name_on_exit(&a);hid_scene_devices_on_enter(&a);
 event(&a,PeerForget);forget_ok=false;event(&a,HidPeerDialogConfirm);assert(a.peer_dialog&&a.peer_store.preferences.selected);forget_ok=true;
 hid_scene_devices_on_enter(&a);event(&a,PeerForget);event(&a,HidPeerDialogConfirm);assert(!a.peer_store.preferences.selected&&!a.peer_active&&forgets==2);
 read_ok=false;hid_scene_devices_on_enter(&a);before=previous;
 assert(hid_scene_devices_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeBack,0}));assert(previous==before+1);read_ok=true;
 a.peer_active=true;a.peer_store.preferences.selected=1;start_ok=false;assert(!hid_peer_restart(&a));assert(!a.peer_active);
 start_ok=true;a.peer_active=true;assert(hid_peer_restart(&a)&&a.peer_active&&!open_pairing);
 hid_scene_devices_on_exit(&a);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
