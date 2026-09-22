"""Native GAP/ACI contract tests for selected-host reconnection (no radio simulation)."""

from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import run_c
from tools.tumoflip.test_toyota_vag_diagnostics import function

ROOT = Path(__file__).resolve().parents[2]

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#define furi_check assert
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_I(...) ((void)0)
#define BLE_STATUS_SUCCESS 0
#define CFG_IDENTITY_ADDRESS 0
#define GAP_RESOLVABLE_PRIVATE_ADDR 2
#define GAP_PERIPHERAL_ROLE 1
#define ADV_IND 0
#define INITIAL_ADV_TIMEOUT 60000
#define GAP_CONNECTION_HANDLE_INVALID UINT16_MAX
typedef int tBleStatus;
typedef enum {GapStateIdle,GapStateAdvFast,GapStateAdvLowPower,GapStateConnected} GapState;
enum {GapPairingNone,GapPairingPinCodeShow,GapPairingPinCodeVerifyYesNo};
enum {GapEventTypeStartAdvertising};
typedef struct {int type;} GapEvent;
typedef struct {uint8_t irk[16],erk[16];} GapRootSecurityKeys;
typedef struct {uint8_t mac_address[6];unsigned appearance_char;int pairing_method;bool bonding_mode;} GapConfig;
typedef struct {unsigned conn_interval,slave_latency,supervisor_timeout;} GapConnectionParams;
typedef struct {GapConfig* config;struct{char* adv_name;unsigned gap_svc_handle,dev_name_char_handle,appearance_char_handle;
 uint16_t connection_handle;uint8_t mfg_data[23],adv_svc_uuid[20];unsigned mfg_data_len,adv_svc_uuid_len;}service;
 bool peer_selection,peer_filter_selected,peer_filter_valid,enable_adv;GapState state;
 void* advertise_timer;void* context;void(*on_event_cb)(GapEvent,void*);GapConnectionParams connection_params;} Gap;
static Gap instance,*gap=&instance;
static GapConfig config;
static unsigned privacy,own_address,filter,discoverable,undirected,security,verified,timers;
static int init_status,adv_status,payload_status;
static uint8_t payload[31],payload_size;
static void event_cb(GapEvent event,void* context){(void)event;(void)context;}
static void prepare(void){memset(gap,0,sizeof(*gap));gap->config=&config;gap->service.adv_name="\x09Remote";
 gap->service.adv_svc_uuid_len=3;gap->service.adv_svc_uuid[0]=3;gap->service.adv_svc_uuid[1]=0x12;gap->service.adv_svc_uuid[2]=0x18;
 gap->service.connection_handle=UINT16_MAX;gap->peer_filter_valid=gap->enable_adv=true;gap->on_event_cb=event_cb;
 privacy=own_address=filter=discoverable=undirected=security=verified=timers=0;
 init_status=adv_status=payload_status=0;payload_size=0;}
int furi_timer_stop(void* t){(void)t;return 0;}
int furi_timer_start(void* t,unsigned n){(void)t;(void)n;timers++;return 0;}
int aci_gap_set_non_discoverable(void){return 0;}
int hci_le_set_scan_response_data(unsigned n,const uint8_t* p){(void)n;(void)p;return 0;}
int aci_gap_set_discoverable(int type,unsigned min,unsigned max,unsigned own,unsigned policy,
 unsigned nl,const uint8_t* name,unsigned sl,const uint8_t* svc,int cmin,int cmax){
 (void)type;(void)min;(void)max;(void)policy;(void)nl;(void)name;(void)sl;(void)svc;(void)cmin;(void)cmax;
 own_address=own;filter=0;discoverable++;return adv_status; // ST: policy ignored here.
}
int aci_gap_update_adv_data(uint8_t n,const uint8_t* data){assert(n<=31);memcpy(payload,data,n);payload_size=n;return payload_status;}
int aci_gap_set_undirected_connectable(unsigned min,unsigned max,unsigned own,unsigned policy){
 (void)min;(void)max;own_address=own;filter=policy;undirected++;return adv_status;}
void gap_verify_connection_parameters(Gap* g){(void)g;verified++;}
int aci_gap_slave_security_req(unsigned handle){assert(handle==0);security++;return 0;}
'''


class BlePeerPrivacyTests(unittest.TestCase):
    def test_pr_and_release_ci_run_the_controller_contract(self):
        for workflow in ("pr-build.yml", "release.yml"):
            with self.subTest(workflow=workflow):
                text = (ROOT / ".github/workflows" / workflow).read_text()
                self.assertIn("tools.tumoflip.test_ble_peer_privacy", text)

    def test_selected_advertising_filters_and_open_pairing_is_explicit(self):
        source = (ROOT / "targets/f7/ble_glue/gap.c").read_text()
        body = function(source, "static void gap_advertise_start(GapState new_state) {")
        run_c(PRELUDE + body + r'''
int main(void){
 prepare();gap->peer_selection=true;gap->peer_filter_selected=true;
 gap_advertise_start(GapStateAdvFast);
 assert(undirected==1 && discoverable==0 && filter==3 && own_address==2);
 assert(payload_size==12 && payload[0]==7 && payload[1]==9 && payload[8]==3 && payload[9]==3);
 assert(gap->state==GapStateAdvFast && timers==1);
 gap_advertise_start(GapStateAdvLowPower);assert(undirected==2 && filter==3);
 prepare();gap->peer_selection=true;gap_advertise_start(GapStateAdvFast);
 assert(discoverable==1 && undirected==0 && filter==0 && own_address==2);
 prepare();gap_advertise_start(GapStateAdvFast);
 assert(discoverable==1 && undirected==0 && own_address==0);
 prepare();gap->peer_selection=true;gap->peer_filter_selected=true;payload_status=1;
 gap_advertise_start(GapStateAdvFast);assert(!discoverable && !undirected && !timers && !gap->enable_adv);
 prepare();gap->peer_selection=true;gap->peer_filter_selected=true;adv_status=1;
 gap_advertise_start(GapStateAdvFast);assert(!discoverable && undirected==1 && !timers && !gap->enable_adv);
 prepare();gap->peer_filter_valid=false;gap_advertise_start(GapStateAdvFast);assert(!discoverable && !undirected);
 prepare();gap->peer_selection=true;gap->peer_filter_selected=true;
 gap->service.adv_name="\x09Name far beyond the advertising payload maximum";
 gap_advertise_start(GapStateAdvFast);assert(!discoverable && !undirected && !gap->enable_adv);
 return 0;
}
''')

    def test_peer_profile_enables_controller_privacy_but_legacy_does_not(self):
        source = (ROOT / "targets/f7/ble_glue/gap.c").read_text()
        start = source.index("gap_init_svc(Gap*")
        signature = source[source.rfind("\n", 0, start) + 1:source.index("{", start) + 1]
        body = function(source, signature)
        run_c(PRELUDE + r'''
enum {CONFIG_DATA_PUBADDR_OFFSET,CONFIG_DATA_PUBADDR_LEN,CONFIG_DATA_RANDOM_ADDRESS_OFFSET,
 CONFIG_DATA_RANDOM_ADDRESS_LEN,CONFIG_DATA_IR_OFFSET,CONFIG_DATA_IR_LEN,CONFIG_DATA_ER_OFFSET,CONFIG_DATA_ER_LEN,
 ALL_PHYS_PREFERENCE,TX_2M_PREFERRED,RX_2M_PREFERRED,MITM_PROTECTION_REQUIRED,USE_FIXED_PIN_FOR_PAIRING_FORBIDDEN,
 IO_CAP_DISPLAY_ONLY,IO_CAP_DISPLAY_YES_NO,MITM_PROTECTION_NOT_REQUIRED,USE_FIXED_PIN_FOR_PAIRING_ALLOWED,
 CFG_SC_SUPPORT,CFG_ENCRYPTION_KEY_SIZE_MIN,CFG_ENCRYPTION_KEY_SIZE_MAX};
unsigned LL_FLASH_GetUDN(void){return 1;}
int aci_hal_write_config_data(int offset,int n,const uint8_t* data){(void)offset;(void)n;(void)data;return 0;}
int aci_hal_set_tx_power_level(int a,int b){(void)a;(void)b;return 0;}
int aci_gatt_init(void){return 0;}
int aci_gap_init(int role,unsigned mode,size_t n,unsigned* svc,unsigned* name,unsigned* appearance){
 (void)role;(void)n;(void)svc;(void)name;(void)appearance;privacy=mode;return init_status;}
int aci_gatt_update_char_value(unsigned a,unsigned b,unsigned c,unsigned n,const uint8_t* data){
 (void)a;(void)b;(void)c;(void)n;(void)data;return 0;}
int hci_le_set_default_phy(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}
int aci_gap_set_io_capability(int a){(void)a;return 0;}
int aci_gap_set_authentication_requirement(int a,int b,int c,int d,int e,int f,int g,int h,int i){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;return 0;}
int aci_gap_configure_whitelist(void){return 0;}
''' + body + r'''
int main(void){GapRootSecurityKeys keys={0};
 prepare();gap->peer_selection=true;assert(gap_init_svc(gap,&keys));assert(privacy==2);
 prepare();assert(gap_init_svc(gap,&keys));assert(privacy==0);
 prepare();gap->peer_selection=true;init_status=1;assert(!gap_init_svc(gap,&keys));
 prepare();config.pairing_method=GapPairingPinCodeShow;assert(gap_init_svc(gap,&keys));
 config.pairing_method=GapPairingPinCodeVerifyYesNo;assert(gap_init_svc(gap,&keys));return 0;}
''')

    def test_enhanced_connection_event_updates_handle_and_security(self):
        source = (ROOT / "targets/f7/ble_glue/gap.c").read_text()
        first = source.index("        case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE:")
        events = source[first:source.index("        default:", first)]
        helper = ""
        if "static void gap_connection_complete(" in source:
            helper = function(source, "static void gap_connection_complete(")
        run_c(PRELUDE + r'''
#define HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE 1
#define HCI_LE_ENHANCED_CONNECTION_COMPLETE_SUBEVT_CODE 10
typedef struct {uint8_t Status;uint16_t Connection_Handle,Conn_Interval,Conn_Latency,Supervision_Timeout;} hci_le_connection_complete_event_rp0;
typedef struct {uint8_t Status;uint16_t Connection_Handle;uint8_t local_rpa[6],peer_rpa[6];
 uint16_t Conn_Interval,Conn_Latency,Supervision_Timeout;} hci_le_enhanced_connection_complete_event_rp0;
typedef struct{unsigned subevent;void* data;} Meta;
''' + helper + r'''
static void event(unsigned code,void* data){Meta packet={code,data};Meta* meta_evt=&packet;
 switch(code){
''' + events + r'''
 default:break;}}
int main(void){
 prepare();config.pairing_method=GapPairingPinCodeShow;
 hci_le_enhanced_connection_complete_event_rp0 enhanced={.Connection_Handle=0,.Conn_Interval=24,.Conn_Latency=2,.Supervision_Timeout=300};
 event(10,&enhanced);assert(gap->state==GapStateConnected && gap->service.connection_handle==0);
 assert(gap->connection_params.conn_interval==24 && gap->connection_params.slave_latency==2 && security==1 && verified==1);
 prepare();enhanced.Status=0x3e;event(10,&enhanced);
 assert(gap->service.connection_handle==UINT16_MAX && !security && !verified);
 prepare();hci_le_connection_complete_event_rp0 legacy={0,0,32,0,400};event(1,&legacy);
 assert(gap->service.connection_handle==0 && gap->connection_params.conn_interval==32 && security==1);
 prepare();legacy.Status=0x3e;event(1,&legacy);assert(gap->service.connection_handle==UINT16_MAX && !security);
 return 0;
}
''')

    def test_hal_peer_start_and_restore_leave_legacy_api_unchanged(self):
        source = (ROOT / "targets/f7/furi_hal/furi_hal_bt.c").read_text()
        body = "\n".join(function(source, signature) for signature in (
            "static FuriHalBleProfileBase* furi_hal_bt_start_app_internal(",
            "FuriHalBleProfileBase* furi_hal_bt_start_app(",
            "FuriHalBleProfileBase* furi_hal_bt_change_app(",
            "FuriHalBleProfileBase* furi_hal_bt_change_app_with_peer_selection(",
        ))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
typedef int FuriHalBleProfileBase,GapConfig,GapRootSecurityKeys;
typedef void* FuriHalBleProfileParams;
typedef void(*GapEventCallback)(void);
typedef struct {void(*get_gap_config)(GapConfig*,void*);FuriHalBleProfileBase*(*start)(void*);}FuriHalBleProfileTemplate;
static FuriHalBleProfileBase instance,*current_profile;
static GapConfig current_config;
static bool mode,ready=true,supported=true,init_ok=true;
static unsigned resets,starts,stops;
static bool ble_glue_is_radio_stack_ready(void){return ready;}
static bool furi_hal_bt_is_gatt_gap_supported(void){return supported;}
static bool gap_init_with_peer_selection(GapConfig*c,const GapRootSecurityKeys*k,GapEventCallback cb,void*ctx,bool peer){
 (void)c;(void)k;(void)cb;(void)ctx;mode=peer;return init_ok;}
static void gap_thread_stop(void){stops++;}
static void furi_hal_bt_reinit(void){resets++;current_profile=NULL;}
static void config_cb(GapConfig*c,void*p){(void)c;(void)p;}
static FuriHalBleProfileBase* start_cb(void*p){(void)p;starts++;return &instance;}
static void event_cb(void){}
''' + body + r'''
int main(void){FuriHalBleProfileTemplate profile={config_cb,start_cb};GapRootSecurityKeys keys=0;
 assert(furi_hal_bt_start_app(&profile,NULL,&keys,event_cb,NULL));assert(!mode && starts==1);
 assert(furi_hal_bt_change_app_with_peer_selection(&profile,NULL,&keys,event_cb,NULL));assert(mode && starts==2 && resets==1);
 assert(furi_hal_bt_change_app(&profile,NULL,&keys,event_cb,NULL));assert(!mode && starts==3 && resets==2);
 init_ok=false;assert(!furi_hal_bt_change_app_with_peer_selection(&profile,NULL,&keys,event_cb,NULL));
 assert(mode && starts==3 && stops==1);
 ready=false;assert(!furi_hal_bt_change_app(&profile,NULL,&keys,event_cb,NULL));assert(starts==3);
 ready=true;supported=false;assert(!furi_hal_bt_change_app(&profile,NULL,&keys,event_cb,NULL));assert(starts==3);
 return 0;}
''')


if __name__ == "__main__":
    unittest.main()
