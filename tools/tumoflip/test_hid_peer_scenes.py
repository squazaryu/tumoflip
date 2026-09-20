"""Execute the actual device-picker scenes with BT/storage failure fixtures."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c
from tools.tumoflip.test_hid_peer_store import stripped

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/hid_app"


class HidPeerScenesTests(unittest.TestCase):
    def test_select_pair_cancel_forget_name_and_restart(self):
        run_c(r'''
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
enum{HidViewDialog,HidViewSubmenu,HidViewTextInput,HidScenePeerName};
enum{HidPeerConnected=0x7000,HidPeerDisconnected,HidPeerNameDone,HidPeerDialogBack,HidPeerDialogConfirm};
enum{SceneManagerEventTypeBack,SceneManagerEventTypeCustom,AlignCenter,AlignTop,DialogExResultRight};
typedef int DialogExResult;
typedef struct{int type;uint32_t event;}SceneManagerEvent;
typedef struct{void*bt,*dialog,*submenu,*view_dispatcher,*scene_manager,*text_input,*ble_hid_profile;int ble_hid_cfg;
 HidPeerStore peer_store;GapBondedDevices peer_list;
 bool peer_dialog,peer_pairing_dialog,peer_confirm_forget,peer_connected,peer_active;
 char peer_name[13],peer_header[32];}Hid;
static void*ble_profile_hid_ext;
static int saves,selects,disconnects,previous,next_scene,view,rows,forgets,starts;
static bool save_ok=true,select_ok=true,read_ok=true,forget_ok=true,start_ok=true,open_pairing;
static GapBondedDevice target;
static void*furi_record_open(const char*n){(void)n;return NULL;}
static void furi_record_close(const char*n){(void)n;}
bool hid_peer_store_save(Storage*s,HidPeerStore*store,const HidPeerPreferences*p){(void)s;saves++;if(save_ok)store->preferences=*p;return save_ok;}
static void bt_disconnect(void*b){(void)b;disconnects++;}
static bool bt_set_connection_peer(void*b,const GapBondedDevice*p){(void)b;selects++;open_pairing=!p;if(p)target=*p;return select_ok;}
static bool bt_forget_bonded_device(void*b,const GapBondedDevice*p){(void)b;assert(p);forgets++;return forget_ok;}
static bool bt_get_bonded_devices(void*b,GapBondedDevices*d){(void)b;memset(d,0,sizeof(*d));if(read_ok){d->count=2;d->devices[0].address[0]=1;d->devices[1].address[0]=2;}return read_ok;}
static void*bt_profile_start_idle(void*b,void*t,void*p){(void)b;(void)t;(void)p;starts++;return start_ok?(void*)1:NULL;}
static void submenu_reset(void*v){(void)v;rows=0;}
static void submenu_set_header(void*v,const char*t){(void)v;assert(strlen(t)<24);}
static void submenu_add_item(void*v,const char*t,uint32_t n,void(*cb)(void*,uint32_t),void*c){(void)v;(void)n;(void)cb;(void)c;assert(strlen(t)<24);rows++;}
static void submenu_set_selected_item(void*v,uint32_t n){(void)v;(void)n;}
static void dialog_ex_reset(void*v){(void)v;}
static void dialog_ex_set_context(void*v,void*c){(void)v;(void)c;}
static void dialog_ex_set_result_callback(void*v,void(*cb)(DialogExResult,void*)){(void)v;(void)cb;}
static void dialog_ex_set_header(void*v,const char*t,int x,int y,int a,int b){(void)v;(void)t;(void)x;(void)y;(void)a;(void)b;}
static void dialog_ex_set_text(void*v,const char*t,int x,int y,int a,int b){(void)v;(void)t;(void)x;(void)y;(void)a;(void)b;}
static void dialog_ex_set_left_button_text(void*v,const char*t){(void)v;(void)t;}
static void dialog_ex_set_right_button_text(void*v,const char*t){(void)v;(void)t;}
static void view_dispatcher_switch_to_view(void*v,int n){(void)v;view=n;}
static void view_dispatcher_send_custom_event(void*v,uint32_t n){(void)v;(void)n;}
static void scene_manager_previous_scene(void*v){(void)v;previous++;}
static void scene_manager_next_scene(void*v,int n){(void)v;next_scene=n;}
static void text_input_reset(void*v){(void)v;}
static void text_input_set_header_text(void*v,const char*t){(void)v;(void)t;}
static void text_input_set_result_callback(void*v,void(*cb)(void*),void*c,char*t,size_t n,bool b){(void)v;(void)cb;(void)c;(void)t;assert(n==13);(void)b;}
''' + stripped(APP / "scenes/hid_scene_devices.c") + "\n" + stripped(APP / "scenes/hid_scene_peer_name.c") + r'''
static void event(Hid*a,uint32_t id){assert(hid_scene_devices_on_event(a,(SceneManagerEvent){SceneManagerEventTypeCustom,id}));}
int main(void){
 Hid a={.ble_hid_profile=(void*)1};hid_scene_devices_on_enter(&a);assert(rows==4&&view==HidViewSubmenu);
 event(&a,PeerFirst);assert(selects==1&&target.address[0]==1&&!open_pairing&&a.peer_active&&saves==1&&previous==1);
 hid_scene_devices_on_enter(&a);assert(rows==6);
 save_ok=false;event(&a,PeerFirst+1);assert(selects==1&&a.peer_dialog&&!a.peer_active);save_ok=true;
 int before=previous;event(&a,HidPeerDialogBack);assert(previous==before+1);
 select_ok=false;event(&a,PeerFirst+1);assert(a.peer_dialog&&!a.peer_active);select_ok=true;
 event(&a,HidPeerDialogBack);event(&a,PeerPair);assert(open_pairing&&a.peer_pairing_dialog);
 before=disconnects;assert(hid_scene_devices_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeBack,0}));
 assert(disconnects==before+1&&!a.peer_pairing_dialog&&view==HidViewSubmenu);
 event(&a,PeerRename);assert(next_scene==HidScenePeerName);hid_scene_peer_name_on_enter(&a);
 strcpy(a.peer_name,"Office PC");assert(hid_scene_peer_name_on_event(&a,(SceneManagerEvent){SceneManagerEventTypeCustom,HidPeerNameDone}));
 assert(a.peer_store.preferences.count==1&&!strcmp(a.peer_store.preferences.labels[0].name,"Office PC"));
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
