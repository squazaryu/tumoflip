#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define furi_assert assert
/* EVENTS */
enum {SubRemSubKeyNameUp,SubRemSubKeyNameDown,SubRemSubKeyNameLeft,
      SubRemSubKeyNameRight,SubRemSubKeyNameOk,SubRemSubKeyNameMaxCount};
enum {SubGhzProtocolTypeUnknown,SubGhzProtocolTypeStatic,SubGhzProtocolTypeDynamic,
      SubGhzProtocolTypeRAW};
enum {SubRemViewRemoteStateIdle,SubRemViewRemoteStateLoading,SubRemViewRemoteStateSending};
enum {SceneManagerEventTypeCustom,SceneManagerEventTypeTick};
enum {SubRemViewIDRemote,SubGhzRadioDeviceTypeInternal};
typedef struct {int type;unsigned event;} SceneManagerEvent;
typedef struct {int type;void* fff_data;} SubRemSubFilePreset;
typedef struct {SubRemSubFilePreset* subs_preset[5];} SubRemMapPreset;
typedef struct {void* transmitter;bool restore_failed;} SubGhzTxRx;
typedef struct {
 SubGhzTxRx* txrx;void* subrem_remote_view;void* notifications;void* scene_manager;
 void* view_dispatcher;void* dialogs;SubRemMapPreset* map_preset;uint8_t chosen_sub;
 bool tx_running,tx_held,save_failed;
} SubGhzRemoteApp;
static int sequence_blink_start_magenta,sequence_blink_red_100,sequence_blink_stop;
static int starts,stops,transition,active=-1;
static bool complete,start_ok=true;
static SubGhzRemoteApp* context;
static struct {bool endless_tx;} subghz_block_generic_global;
#define endless subghz_block_generic_global.endless_tx
enum {SUBGHZ_CUSTOM_BTN_OK};
static int subghz_custom_btn_get(void){return SUBGHZ_CUSTOM_BTN_OK;}
static void subghz_custom_btn_set(int b){(void)b;}
static void subghz_txrx_custom_button_reset(SubGhzTxRx* t){(void)t;}
static void subghz_transmitter_deserialize(void* t,void* f){(void)t;(void)f;}
static void subghz_custom_btns_reset(void){}
static void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* t){(void)t;}
static void subrem_view_remote_set_state(void* v,int s,int i){(void)v;(void)s;(void)i;}
static void notification_message(void* n,const int* seq){(void)n;(void)seq;}
static void dialog_message_show_storage_error(void* d,const char* s){(void)d;(void)s;}
static bool scene_manager_previous_scene(void* m){(void)m;assert(active==-1);transition++;return true;}
static void scene_manager_stop(void* m){(void)m;}
static void view_dispatcher_stop(void* d){(void)d;}
static void view_dispatcher_send_custom_event(void* d,int e){(void)d;(void)e;}
static void view_dispatcher_switch_to_view(void* d,int e){(void)d;(void)e;}
static void subrem_view_remote_update_data_labels(void* v,void* p){(void)v;(void)p;}
static void subrem_view_remote_set_radio(void* v,bool e){(void)v;(void)e;}
static int subghz_txrx_radio_device_get(void* p){(void)p;return 0;}
static void subrem_view_remote_set_callback(void* v,void(*cb)(SubRemCustomEvent,void*),void* c){(void)v;(void)cb;(void)c;}
static void subghz_txrx_set_raw_file_encoder_worker_callback_end(void* t,void(*cb)(void*),void* c){(void)t;(void)cb;(void)c;}
static bool subghz_txrx_tx_is_complete(void* t){(void)t;return complete;}
static bool subrem_tx_start_sub(SubGhzRemoteApp* a,SubRemSubFilePreset* p){
 (void)p;assert(active==-1);starts++;if(!start_ok)return false;
 active=a->chosen_sub;a->tx_running=true;endless=p->type!=SubGhzProtocolTypeRAW;return true;
}
static void subghz_txrx_stop(SubGhzTxRx* t){
 (void)t;if(active!=-1){assert(active==context->chosen_sub);stops++;active=-1;}
 endless=false;
}
/* STOP */
/* SCENE */
static void send(SubGhzRemoteApp* a,int type,int event){
 subrem_scene_remote_on_event(a,(SceneManagerEvent){type,(unsigned)event});
}
int main(void){
 SubRemSubFilePreset dynamic={SubGhzProtocolTypeDynamic},raw={SubGhzProtocolTypeRAW};
 SubRemMapPreset map={{&dynamic,&raw,&dynamic,&dynamic,&dynamic}};
 SubGhzTxRx radio={0};SubGhzRemoteApp app={.map_preset=&map,.txrx=&radio};context=&app;
 subrem_scene_remote_on_enter(&app);
 send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStartUP);
 assert(starts==1&&active==0);
 send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStop);
 assert(active==0&&stops==0&&!endless); // release must finish the current frame
 complete=true;send(&app,SceneManagerEventTypeTick,0);assert(active==-1&&stops==1);
 complete=false;send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStartDOWN);
 complete=true;send(&app,SceneManagerEventTypeTick,0);assert(active==1&&starts==3);
 send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStop);
 send(&app,SceneManagerEventTypeTick,0);assert(active==-1);
 complete=false;send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStartUP);
 send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStartDOWN);
 assert(active==1); // old entry was stopped before chosen_sub was changed
 send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteBack);
 assert(active==-1&&transition==1&&!app.tx_held&&!app.tx_running);
 start_ok=false;send(&app,SceneManagerEventTypeCustom,SubRemCustomEventViewRemoteStartUP);
 int before=starts;send(&app,SceneManagerEventTypeTick,0);
 assert(starts==before&&active==-1&&!app.tx_held&&!app.tx_running);
 subrem_scene_remote_on_exit(&app);assert(!endless);return 0;
}
