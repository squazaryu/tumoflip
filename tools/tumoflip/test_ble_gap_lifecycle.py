"""Execute real GAP initialization, disconnect handling and peer readiness together."""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_toyota_vag_diagnostics import function
from tools.tumoflip.test_nfc_completion_equality import native as run_c

ROOT = Path(__file__).resolve().parents[2]


class BleGapLifecycleTests(unittest.TestCase):
    def test_cold_start_select_pair_forget_and_zero_connection_handle(self):
        source = (ROOT / "targets/f7/ble_glue/gap.c").read_text()
        constants = "\n".join(re.findall(r"^#define GAP_CONNECTION_HANDLE_INVALID.*$", source, re.M))
        disconnect = source[source.index("    case HCI_DISCONNECTION_COMPLETE_EVT_CODE:"):
                            source.index("    case HCI_LE_META_EVT_CODE:")]
        production = "\n".join(function(source, signature) for signature in (
            "static bool gap_wait_peer_idle(", "bool gap_set_connection_peer(",
            "bool gap_forget_bonded_device(", "bool gap_init_with_peer_selection(", "bool gap_init(",
        ))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
static void test_furi_check(bool condition){assert(condition);}
#define furi_check(condition) test_furi_check(condition)
#define furi_crash(...) abort()
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define FuriWaitForever 0
#define FuriTimerTypeOnce 0
#define FuriMutexTypeNormal 0
#define HCI_DISCONNECTION_COMPLETE_EVT_CODE 5
#define GapEventTypeDisconnected 2
enum {GapStateIdle,GapStateConnected,GapStateAdvFast};
enum {UUID_TYPE_16,UUID_TYPE_128};
typedef int GapCommand;
typedef struct {uint8_t address_type,address[6];} GapBondedDevice;
typedef struct {int UUID_Type;uint16_t Service_UUID_16;uint8_t Service_UUID_128[16];} AdvService;
typedef struct {char* adv_name;int mfg_data_len;uint8_t* mfg_data;AdvService adv_service;} GapConfig;
typedef int GapRootSecurityKeys;
typedef struct {int type;} GapEvent;
typedef void (*GapEventCallback)(GapEvent,void*);
typedef struct {GapConfig* config;struct{uint16_t connection_handle;char* adv_name;int mfg_data_len,adv_svc_uuid_len;}service;
 void *advertise_timer,*state_mutex,*command_queue,*thread,*context;GapEventCallback on_event_cb;
 int state,negotiation_round;bool enable_adv,is_secure,peer_filter_selected,peer_filter_valid,peer_selection;}Gap;
static Gap* gap;
static uint32_t ticks;
static int selects,forgets;
static bool stack_ready=true,controller_ok=true,init_ok=true;
static uint32_t furi_get_tick(void){return ticks;}
static uint32_t furi_ms_to_ticks(uint32_t ms){return ms;}
static void furi_delay_ms(uint32_t ms){ticks+=ms;}
static void furi_delay_us(uint32_t us){(void)us;}
static void furi_mutex_acquire(void* m,int wait){(void)m;(void)wait;}
static void furi_mutex_release(void* m){(void)m;}
static bool gap_peer_select(const GapBondedDevice* p){(void)p;selects++;return controller_ok;}
static bool gap_peer_forget(const GapBondedDevice* p){assert(p);forgets++;return controller_ok;}
static bool ble_glue_is_radio_stack_ready(void){return stack_ready;}
static void gap_advertise_timer_callback(void* context){(void)context;}
static void* furi_timer_alloc(void(*cb)(void*),int type,void* c){(void)cb;(void)type;(void)c;return NULL;}
static bool gap_init_svc(Gap* g,const GapRootSecurityKeys* keys){(void)g;(void)keys;return init_ok;}
static void ble_event_dispatcher_init(void){}
static void* furi_mutex_alloc(int type){(void)type;return NULL;}
static void* furi_message_queue_alloc(int n,size_t size){(void)n;(void)size;return NULL;}
static int32_t gap_app(void* c){(void)c;return 0;}
static void* furi_thread_alloc_ex(const char* n,int size,int32_t(*entry)(void*),void* c){(void)n;(void)size;(void)entry;(void)c;return NULL;}
static void furi_thread_start(void* thread){(void)thread;}
static void set_manufacturer_data(uint8_t* p,int n){(void)p;(void)n;}
static void set_advertisment_service_uid(uint8_t* p,size_t n){(void)p;(void)n;}
static void gap_advertise_start(int state){(void)state;}
static void event_callback(GapEvent e,void* c){(void)e;(void)c;}
typedef struct {uint16_t Connection_Handle;uint8_t Reason;} hci_disconnection_complete_event_rp0;
typedef struct {void* data;} EventPacket;
''' + constants + "\n" + production + r'''
static void disconnect_event(uint16_t handle){
    hci_disconnection_complete_event_rp0 payload={handle,0x13};
    EventPacket packet={&payload};EventPacket* event_pckt=&packet;
    switch(HCI_DISCONNECTION_COMPLETE_EVT_CODE){
''' + disconnect + r'''
    }
}
int main(void){
    GapConfig config={.adv_service={.UUID_Type=UUID_TYPE_16}};GapBondedDevice peer={0};
    stack_ready=false;assert(!gap_init(&config,NULL,event_callback,NULL));assert(!gap);
    stack_ready=true;assert(gap_init(&config,NULL,event_callback,NULL));
    assert(gap->service.connection_handle==UINT16_MAX);
    assert(gap_set_connection_peer(&peer));assert(selects==1 && ticks==0 && gap->peer_filter_selected);
    assert(gap_set_connection_peer(NULL));assert(selects==2 && !gap->peer_filter_selected);
    assert(gap_forget_bonded_device(&peer));assert(forgets==1);
    // HCI connection handle zero is valid. Idle alone is insufficient during async disconnect.
    gap->state=GapStateIdle;gap->service.connection_handle=0;gap->enable_adv=false;
    assert(!gap_set_connection_peer(&peer) && selects==2 && ticks==500);
    disconnect_event(7);assert(gap->service.connection_handle==0);
    assert(!gap_forget_bonded_device(&peer) && forgets==1);
    disconnect_event(0);assert(gap->service.connection_handle==UINT16_MAX);
    assert(gap_set_connection_peer(&peer) && selects==3);
    gap->state=GapStateConnected;assert(!gap_set_connection_peer(NULL));
    gap->state=GapStateIdle;controller_ok=false;
    assert(!gap_set_connection_peer(&peer) && !gap->peer_filter_valid && !gap->enable_adv);
    assert(!gap_forget_bonded_device(&peer));assert(!gap_forget_bonded_device(NULL));
    ticks=UINT32_MAX-10;gap->service.connection_handle=0;
    assert(!gap_set_connection_peer(&peer));assert((uint32_t)(ticks-(UINT32_MAX-10))==500);
    free(gap);gap=NULL;assert(!gap_set_connection_peer(NULL));assert(!gap_forget_bonded_device(&peer));
    config.mfg_data_len=1;config.adv_service.UUID_Type=UUID_TYPE_128;
    controller_ok=true;assert(gap_init(&config,NULL,event_callback,NULL));
    assert(gap_set_connection_peer(&peer));assert(!gap->peer_selection);free(gap);gap=NULL;
    init_ok=false;assert(!gap_init_with_peer_selection(&config,NULL,event_callback,NULL,true));assert(!gap);
    init_ok=true;assert(gap_init_with_peer_selection(&config,NULL,event_callback,NULL,true));
    assert(gap->peer_selection);free(gap);gap=NULL;
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
