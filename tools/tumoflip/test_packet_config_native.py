"""Bounded CC1101 packet configuration and channel arithmetic."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c, function

ROOT = Path(__file__).resolve().parents[2]


class PacketConfigNativeTests(unittest.TestCase):
    def test_worker_copies_custom_preset_and_refuses_live_reconfiguration(self):
        source = (ROOT / "lib/subghz/subghz_tx_rx_worker.c").read_text()
        code = function(source, "bool subghz_tx_rx_worker_set_packet_preset(")
        code += function(source, "bool subghz_tx_rx_worker_set_packet_channel(")
        run_c('#include <assert.h>\n#include <string.h>\n'
              + f'#include "{ROOT}/lib/subghz/packet_config.c"\n' + r'''
typedef enum {FuriHalSubGhzPresetGFSK9_99KbAsync=7,FuriHalSubGhzPresetCustom=8} FuriHalSubGhzPreset;
typedef struct {bool thread_started;FuriHalSubGhzPreset preset;uint8_t preset_data[256];
 size_t preset_size;uint8_t channel;} SubGhzTxRxWorker;
''' + code + r'''
int main(void) {
 SubGhzTxRxWorker w={0};uint8_t bytes[]={2,6,8,5,0,0,0x12,0,0,0,0,0,0,0};
 assert(subghz_tx_rx_worker_set_packet_preset(&w,FuriHalSubGhzPresetCustom,bytes,sizeof(bytes)));
 bytes[1]=99;assert(w.preset_data[1]==6&&w.preset_size==sizeof(bytes));
 assert(!subghz_tx_rx_worker_set_packet_preset(&w,FuriHalSubGhzPresetCustom,bytes,1));
 assert(w.preset_data[1]==6);
 assert(subghz_tx_rx_worker_set_packet_channel(&w,255)&&w.channel==255);
 w.thread_started=true;
 assert(!subghz_tx_rx_worker_set_packet_channel(&w,1)&&w.channel==255);
 assert(!subghz_tx_rx_worker_set_packet_preset(&w,FuriHalSubGhzPresetGFSK9_99KbAsync,NULL,0));
 w.thread_started=false;
 assert(subghz_tx_rx_worker_set_packet_preset(&w,FuriHalSubGhzPresetGFSK9_99KbAsync,NULL,0));
 assert(!w.preset_size);
 assert(!subghz_tx_rx_worker_set_packet_preset(&w,0,NULL,0));
 assert(!subghz_tx_rx_worker_set_packet_channel(NULL,1));
 return 0;
}
''')

    def test_real_channel_adapters_validate_and_rollback_without_crashing(self):
        internal = (ROOT / "targets/f7/furi_hal/furi_hal_subghz.c").read_text()
        external = (ROOT / "applications/drivers/subghz/cc1101_ext/cc1101_ext.c").read_text()
        code = function(internal, "bool furi_hal_subghz_set_channel_checked(")
        code += function(external, "bool subghz_device_cc1101_ext_set_channel(")
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int FuriHalSpiBusHandle;
enum {SubGhzStateIdle,SubGhzDeviceCC1101ExtStateIdle=0,CC1101StateIDLE};
static const FuriHalSpiBusHandle furi_hal_spi_bus_handle_subghz=0;
static struct {int state;} furi_hal_subghz={0};
typedef struct {int state;const FuriHalSpiBusHandle*spi_bus_handle;} ExternalDevice;
static ExternalDevice external_device={0,&furi_hal_spi_bus_handle_subghz};
static ExternalDevice* subghz_device_cc1101_ext=&external_device;
static bool allowed,calibrated;
static uint8_t current_channel;
static unsigned acquired,released;
void furi_hal_spi_acquire(const FuriHalSpiBusHandle*s){(void)s;acquired++;}
void furi_hal_spi_release(const FuriHalSpiBusHandle*s){(void)s;released++;}
uint32_t cc1101_get_channel_frequency(const FuriHalSpiBusHandle*s,uint8_t c){(void)s;(void)c;return 433920000;}
bool furi_hal_subghz_is_frequency_valid(uint32_t f){(void)f;return true;}
bool furi_hal_subghz_is_tx_allowed(uint32_t f){(void)f;return allowed;}
bool subghz_device_cc1101_ext_is_frequency_valid(uint32_t f){(void)f;return true;}
bool subghz_device_cc1101_ext_is_tx_allowed(uint32_t f){(void)f;return allowed;}
uint8_t cc1101_get_channel(const FuriHalSpiBusHandle*s){(void)s;return current_channel;}
void cc1101_switch_to_idle(const FuriHalSpiBusHandle*s){(void)s;}
void cc1101_set_channel(const FuriHalSpiBusHandle*s,uint8_t c){(void)s;current_channel=c;}
void cc1101_calibrate(const FuriHalSpiBusHandle*s){(void)s;}
bool cc1101_wait_status_state(const FuriHalSpiBusHandle*s,int state,uint32_t us){
 (void)s;(void)state;assert(us==10000);return calibrated;}
''' + code + r'''
int main(void) {
 bool(*setters[])(uint8_t)={furi_hal_subghz_set_channel_checked,subghz_device_cc1101_ext_set_channel};
 for(unsigned i=0;i<2;i++) {
  current_channel=7;acquired=released=0;allowed=false;calibrated=true;
  assert(!setters[i](13)&&current_channel==7&&acquired==1&&released==1);
  allowed=true;calibrated=false;
  assert(!setters[i](13)&&current_channel==7&&acquired==2&&released==2);
  calibrated=true;
  assert(setters[i](13)&&current_channel==13&&acquired==3&&released==3);
 }
 acquired=released=0;
 subghz_device_cc1101_ext=NULL;
 assert(!subghz_device_cc1101_ext_set_channel(13)&&acquired==0&&released==0);
 subghz_device_cc1101_ext=&external_device;
 external_device.state=1;
 assert(!subghz_device_cc1101_ext_set_channel(13)&&acquired==0&&released==0);
 return 0;
}
''')

    def test_prepare_releases_every_partial_radio_initialization(self):
        source = (ROOT / "lib/subghz/subghz_tx_rx_worker.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int SubGhzRadioBroker;
typedef int GpioPin;
typedef struct {uint32_t token;} SubGhzRadioBrokerLease;
typedef struct {bool begin;} Interconnect;
typedef struct {Interconnect* interconnect;} SubGhzDevice;
typedef struct {SubGhzRadioBroker*broker;SubGhzRadioBrokerLease lease;bool device_begun,borrowed_lease,thread_started;
 const SubGhzDevice*device;const GpioPin*device_data_gpio;int preset;uint8_t preset_data[256];
 size_t preset_size;uint8_t channel;uint32_t frequency;} SubGhzTxRxWorker;
#define RECORD_SUBGHZ_RADIO_BROKER "broker"
enum {GpioModeInput,GpioPullNo,GpioSpeedLow,SubGhzRadioBrokerStateInitialized,
      SubGhzRadioBrokerStateProbing,SubGhzRadioBrokerStateAcquired};
static unsigned scenario,ends,closes,releases;
static SubGhzRadioBroker broker;
static GpioPin pin;
void* furi_record_open(const char* s) {(void)s;return &broker;}
void furi_record_close(const char* s) {(void)s;closes++;}
uint32_t furi_ms_to_ticks(uint32_t n) {return n;}
bool subghz_radio_broker_acquire(void*b,const char*n,uint32_t t,SubGhzRadioBrokerLease*l) {
 (void)b;(void)n;(void)t;if(scenario==1)return false;l->token=1;return true;
}
void subghz_radio_broker_release(void*b,SubGhzRadioBrokerLease*l) {(void)b;l->token=0;releases++;}
bool subghz_devices_begin(const SubGhzDevice*d) {(void)d;return scenario!=2;}
void subghz_devices_end(const SubGhzDevice*d) {(void)d;ends++;}
void subghz_devices_sleep(const SubGhzDevice*d) {(void)d;}
const GpioPin* subghz_devices_get_data_gpio(const SubGhzDevice*d) {(void)d;return scenario==3?NULL:&pin;}
void subghz_devices_reset(const SubGhzDevice*d) {(void)d;}
void subghz_devices_idle(const SubGhzDevice*d) {(void)d;}
void subghz_devices_load_preset(const SubGhzDevice*d,int p,uint8_t*x) {(void)d;(void)p;(void)x;}
uint32_t subghz_devices_set_frequency(const SubGhzDevice*d,uint32_t f) {(void)d;return scenario==4?0:f;}
bool subghz_devices_set_channel_checked(const SubGhzDevice*d,uint8_t c) {(void)d;(void)c;return scenario!=5;}
void furi_hal_gpio_init(const GpioPin*p,int a,int b,int c) {(void)p;(void)a;(void)b;(void)c;}
void subghz_devices_flush_rx(const SubGhzDevice*d) {(void)d;}
bool subghz_radio_broker_set_state(void*b,SubGhzRadioBrokerLease*l,int s) {(void)b;(void)l;(void)s;return true;}
''' + function(source, "static void subghz_tx_rx_worker_release(")
        + function(source, "static bool subghz_tx_rx_worker_prepare(") + r'''
int main(void) {
 Interconnect callbacks={.begin=true};SubGhzDevice device={.interconnect=&callbacks};
 for(scenario=0;scenario<=5;scenario++) {
    ends=closes=releases=0;SubGhzTxRxWorker w={0};
    bool ok=subghz_tx_rx_worker_prepare(&w,&device,433920000);
    assert(ok==(scenario==0));
    if(ok){assert(w.broker&&w.device_begun);subghz_tx_rx_worker_release(&w);}
    assert(!w.broker&&!w.device_begun&&closes==1);
    assert(releases==(scenario==1?0:1));
    assert(ends==(scenario==1?0:1));
 }
 scenario=0;ends=closes=releases=0;
 SubGhzTxRxWorker borrowed={.broker=&broker,.lease={77},.borrowed_lease=true};
 assert(subghz_tx_rx_worker_prepare(&borrowed,&device,433920000));
 subghz_tx_rx_worker_release(&borrowed);
 assert(ends==1&&closes==0&&releases==0&&!borrowed.broker);
 return 0;
}
''')

    def test_rejected_frequency_never_starts_thread(self):
        source = (ROOT / "lib/subghz/subghz_tx_rx_worker.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef int SubGhzDevice;
typedef struct {bool worker_running,thread_started,startup_ok;void *stream_tx,*stream_rx,*thread,*startup_ready;uint32_t frequency;const SubGhzDevice* device;} SubGhzTxRxWorker;
#define furi_check assert
static unsigned threads;
void furi_stream_buffer_reset(void* p) {(void)p;}
bool furi_hal_subghz_is_tx_allowed(uint32_t f) {(void)f;return false;}
void furi_thread_start(void* p) {(void)p;threads++;}
void furi_thread_join(void* p) {(void)p;}
enum {FuriStatusOk};
uint32_t furi_ms_to_ticks(uint32_t t) {return t;}
int furi_semaphore_acquire(void*p,uint32_t t) {(void)p;(void)t;return FuriStatusOk;}
bool subghz_devices_is_frequency_valid(const SubGhzDevice* d,uint32_t f) {(void)d;(void)f;return true;}
bool subghz_tx_rx_worker_prepare(SubGhzTxRxWorker* w,const SubGhzDevice* d,uint32_t f) {(void)w;(void)d;(void)f;return true;}
''' + function(source, "bool subghz_tx_rx_worker_start(") + r'''
int main(void) {
    SubGhzTxRxWorker w={0};SubGhzDevice d=0;
    assert(!subghz_tx_rx_worker_start(&w,&d,100));
    assert(threads==0 && !w.worker_running);
    return 0;
}
''')

    def test_bounded_packet_preset_and_channel_frequency(self):
        run_c('#include <assert.h>\n#include <string.h>\n'
              + f'#include "{ROOT}/lib/subghz/packet_config.c"\n' + r'''
int main(void) {
    uint8_t good[]={2,6,8,5,0,0,0x12,0,0,0,0,0,0,0};
    assert(subghz_packet_preset_is_valid(good,sizeof(good)));
    for(size_t n=0;n<sizeof(good);n++)assert(!subghz_packet_preset_is_valid(good,n));
    assert(!subghz_packet_preset_is_valid(NULL,sizeof(good)));
    uint8_t wrong[sizeof(good)];memcpy(wrong,good,sizeof(good));
    wrong[3]=4;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // fixed length
    wrong[3]=1;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // CRC absent
    wrong[3]=0x35;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // async format
    wrong[3]=5;wrong[1]=0;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // no sync GDO0
    wrong[1]=6;wrong[0]=0x40;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // invalid register
    uint8_t trailing[sizeof(good)+1];memcpy(trailing,good,sizeof(good));trailing[sizeof(good)]=0;
    assert(!subghz_packet_preset_is_valid(trailing,sizeof(trailing)));
    uint8_t oversized[257]={0};assert(!subghz_packet_preset_is_valid(oversized,sizeof(oversized)));
    assert(subghz_packet_channel_frequency(0x10b071,0,0,0)==433919830);
    assert(subghz_packet_channel_frequency(0x10b071,0,0,1)==433945220);
    assert(subghz_packet_channel_frequency(0x10b071,255,3,255)==537311248);
    assert(subghz_packet_channel_frequency(0x1000000,0,0,0)==0); // invalid 24-bit word
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
