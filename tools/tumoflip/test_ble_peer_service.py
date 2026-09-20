"""Execute BT-service switching/peer requests, including legacy profile behavior."""
from pathlib import Path
import unittest
from tools.tumoflip.test_toyota_vag_diagnostics import function
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class BlePeerServiceTests(unittest.TestCase):
    def test_profile_storage_advertising_and_forget_failures(self):
        source = (ROOT / "applications/services/bt/bt_service/bt.c").read_text()
        body = "\n".join(function(source, name) for name in (
            "static void bt_change_profile(", "static void bt_close_connection(",
            "static void bt_handle_peer_request("))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
typedef struct{int id;}FuriHalBleProfileBase;
typedef struct{bool enabled;}BtSettings;
typedef struct{BtSettings bt_settings;void*keys_storage;FuriHalBleProfileBase*current_profile;uint8_t*bt_keys_addr_start;uint16_t bt_keys_size;}Bt;
enum{BtMessageTypeGetBondedDevices,BtMessageTypeSetConnectionPeer,BtMessageTypeForgetBondedDevice};
typedef struct{int type;bool*result;FuriHalBleProfileBase**profile_instance;
 struct{struct{void*template,*params;bool start_idle;}profile;void*peer,*bonded_devices;}data;}BtMessage;
static FuriHalBleProfileBase serial={1},hid={2};static void*ble_profile_serial=&serial;
static void*bt_on_gap_event_callback;static void*bt_on_key_storage_change_callback;
static bool supported=true,strict_ok=true,select_ok=true,forget_ok=true,save_ok=true;
static int changes,starts,stops,strict_loads,legacy_loads,saves,forgets;
static bool furi_hal_bt_is_gatt_gap_supported(void){return supported;}
static void bt_settings_load(BtSettings*s){(void)s;}
static void bt_close_rpc_connection(Bt*b){(void)b;}
static void furi_hal_bt_stop_advertising(void){stops++;}
static bool bt_keys_storage_load_or_create(void*k){(void)k;strict_loads++;return strict_ok;}
static bool bt_keys_storage_load(void*k){(void)k;legacy_loads++;return false;}
static void*bt_keys_storage_get_root_keys(void*k){return k;}
static FuriHalBleProfileBase*furi_hal_bt_change_app(void*t,void*p,void*k,void*cb,void*c){(void)t;(void)p;(void)k;(void)cb;(void)c;changes++;return &hid;}
static void bt_app_bridge_bind(Bt*b){(void)b;}
static void furi_hal_bt_start_advertising(void){starts++;}
static void furi_hal_bt_set_key_storage_change_callback(void*c,void*b){(void)c;(void)b;}
static void bt_show_warning(Bt*b,const char*s){(void)b;(void)s;}
static bool furi_hal_bt_check_profile_type(void*p,void*t){return p==t;}
static bool gap_get_bonded_devices(void*d){assert(d);return true;}
static bool gap_set_connection_peer(const void*p){(void)p;return select_ok;}
static bool gap_forget_bonded_device(const void*p){assert(p);forgets++;return forget_ok;}
static bool bt_keys_storage_update(void*k,uint8_t*p,uint32_t n){(void)k;(void)p;assert(n==16);saves++;return save_ok;}
''' + body + r'''
int main(void){
 Bt b={.bt_settings={true},.current_profile=&serial,.bt_keys_size=16};
 bool ok=true;FuriHalBleProfileBase*out=&hid;BtMessage m={.result=&ok,.profile_instance=&out};
 m.data.profile.start_idle=true;strict_ok=false;bt_change_profile(&b,&m);
 assert(!ok&&!out&&b.current_profile==&serial&&!changes&&!starts&&stops==1);
 strict_ok=true;bt_change_profile(&b,&m);assert(ok&&out==&hid&&changes==1&&!starts);
 m.data.profile.start_idle=false;bt_change_profile(&b,&m);assert(ok&&legacy_loads==1&&starts==1);
 b.bt_settings.enabled=false;bt_change_profile(&b,&m);assert(starts==1);
 m.type=BtMessageTypeGetBondedDevices;m.data.bonded_devices=&b;b.current_profile=&serial;bt_handle_peer_request(&b,&m);assert(!ok);
 b.current_profile=&hid;bt_handle_peer_request(&b,&m);assert(ok);
 m.type=BtMessageTypeSetConnectionPeer;m.data.peer=&b;select_ok=false;bt_handle_peer_request(&b,&m);assert(!ok&&starts==1);
 select_ok=true;bt_handle_peer_request(&b,&m);assert(ok&&starts==2);
 m.type=BtMessageTypeForgetBondedDevice;save_ok=false;bt_handle_peer_request(&b,&m);assert(!ok&&saves==1&&forgets==1&&starts==2);
 save_ok=true;bt_handle_peer_request(&b,&m);assert(ok&&saves==2&&starts==2);
 supported=false;bt_handle_peer_request(&b,&m);assert(!ok&&saves==2);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
