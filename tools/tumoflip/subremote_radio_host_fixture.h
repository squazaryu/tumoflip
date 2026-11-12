#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
enum {SubGhzTxRxStateIDLE,SubGhzTxRxStateTx,SubGhzTxRxStateSleep};
enum {SubGhzTxRxStartTxStateOk,SubGhzTxRxStartTxStateErrorParserOthers,SubGhzTxRxStartTxStateErrorOnlyRx};
enum {SubGhzProtocolStatusOk,SubGhzProtocolStatusError};
enum {SubGhzRadioBrokerStateAsyncTx,SubGhzRadioBrokerStateCleaningUp};
enum {SubGhzProtocolTypeDynamic};
enum {SUBGHZ_CUSTOM_BTN_OK};
typedef int SubGhzTxRxStartTxState;
typedef struct {int unused;} FuriString;
typedef struct {int unused;} FlipperFormat;
typedef struct {FuriString* name;uint32_t frequency;} SubGhzRadioPreset;
typedef struct {int type;} Protocol;
typedef struct {Protocol* protocol;} Decoder;
typedef struct {
 int txrx_state;void* transmitter;void* environment;void* setting;void* radio_device;
 FlipperFormat* tx_data;bool restore_failed;SubGhzRadioPreset* preset;Decoder* decoder_result;
 void(*need_save_callback)(void*);void* need_save_context;
} SubGhzTxRx;
static unsigned deserializes,rf_starts,saves,custom_button,counter;
static int32_t multiplier=7;
static bool off=true,preset_valid=true,async_ok=true,alloc_ok=true,restore_ok=true;
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static const char* furi_string_get_cstr(FuriString* s){(void)s;return "dummy";}
static void furi_string_free(FuriString* s){free(s);}
static bool flipper_format_rewind(FlipperFormat* f){(void)f;return true;}
static bool flipper_format_read_string(FlipperFormat* f,const char* k,FuriString* s){(void)f;(void)k;(void)s;return true;}
static void* subghz_transmitter_alloc_init(void* e,const char* p){(void)e;(void)p;return alloc_ok?malloc(1):NULL;}
static int subghz_transmitter_deserialize(void* t,FlipperFormat* f){
 assert(t&&f&&off);deserializes++;counter+=multiplier;
 return restore_ok?SubGhzProtocolStatusOk:SubGhzProtocolStatusError;
}
static void subghz_transmitter_free(void* t){assert(t);free(t);}
static void subghz_transmitter_stop(void* t){assert(t&&off);}
static void subghz_transmitter_yield(void){}
static uint8_t* subghz_setting_get_preset_data_by_name(void* s,const char* n){(void)s;(void)n;static uint8_t p;return preset_valid?&p:NULL;}
static void subghz_txrx_begin(SubGhzTxRx* t,uint8_t* p){(void)t;(void)p;}
static bool subghz_txrx_tx(SubGhzTxRx* t,uint32_t f){assert(f);t->txrx_state=SubGhzTxRxStateTx;return true;}
static bool subghz_devices_start_async_tx(void* d,void(*y)(void),void* t){(void)d;(void)y;assert(t&&off);rf_starts++;off=!async_ok;return async_ok;}
static void subghz_devices_stop_async_tx(void* d){(void)d;off=true;}
static void subghz_txrx_radio_state(SubGhzTxRx* t,int s){(void)t;(void)s;}
static void subghz_txrx_idle(SubGhzTxRx* t){assert(off);t->txrx_state=SubGhzTxRxStateIDLE;}
static void subghz_txrx_speaker_off(SubGhzTxRx* t){(void)t;}
static unsigned subghz_custom_btn_get(void){return custom_button;}
static void subghz_custom_btn_set(unsigned b){custom_button=b;}
static int32_t furi_hal_subghz_get_rolling_counter_mult(void){return multiplier;}
static void furi_hal_subghz_set_rolling_counter_mult(int32_t m){multiplier=m;}
static void saved(void* c){(void)c;assert(off);saves++;}
static void subghz_txrx_stop(SubGhzTxRx* t);
/* RADIO */
static void subghz_txrx_stop(SubGhzTxRx* t){if(t->txrx_state==SubGhzTxRxStateTx)subghz_txrx_tx_stop(t);}
int main(void){
 FuriString name={0};SubGhzRadioPreset preset={&name,433920000};
 Protocol protocol={SubGhzProtocolTypeDynamic};Decoder decoder={&protocol};FlipperFormat data={0};
 SubGhzTxRx t={.preset=&preset,.decoder_result=&decoder,.need_save_callback=saved};
 assert(subghz_txrx_tx_start(&t,&data)==SubGhzTxRxStartTxStateOk);
 assert(deserializes==1&&counter==7&&rf_starts==1&&!off);
 custom_button=1;subghz_txrx_stop(&t);
 assert(deserializes==3);
 assert(off&&rf_starts==1&&counter==7&&multiplier==7&&saves==1&&!t.transmitter&&!t.tx_data);
 unsigned count=deserializes;subghz_txrx_stop(&t);assert(deserializes==count);
 preset_valid=false;assert(subghz_txrx_tx_start(&t,&data)!=SubGhzTxRxStartTxStateOk);
 assert(off&&rf_starts==1&&!t.transmitter&&!t.tx_data);
 preset_valid=true;async_ok=false;
 assert(subghz_txrx_tx_start(&t,&data)!=SubGhzTxRxStartTxStateOk);
 assert(off&&rf_starts==2&&!t.transmitter&&!t.tx_data);
 alloc_ok=false;assert(subghz_txrx_tx_start(&t,&data)!=SubGhzTxRxStartTxStateOk);
 assert(off&&!t.transmitter);
 alloc_ok=true;async_ok=true;assert(subghz_txrx_tx_start(&t,&data)==SubGhzTxRxStartTxStateOk);
 unsigned rf_before=rf_starts,saves_before=saves,counter_before=counter;
 restore_ok=false;custom_button=1;subghz_txrx_stop(&t);
 assert(t.restore_failed&&saves==saves_before&&rf_starts==rf_before&&counter==counter_before&&multiplier==7);
 return 0;
}
