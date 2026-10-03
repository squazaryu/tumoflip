"""Bounded CC1101 packet configuration and channel arithmetic."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c, function

ROOT = Path(__file__).resolve().parents[2]


class PacketConfigNativeTests(unittest.TestCase):
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
typedef struct {SubGhzRadioBroker*broker;SubGhzRadioBrokerLease lease;bool device_begun;
 const SubGhzDevice*device;const GpioPin*device_data_gpio;int preset;uint8_t preset_data[256];
 size_t preset_size;uint8_t channel;uint32_t frequency;} SubGhzTxRxWorker;
#define RECORD_SUBGHZ_RADIO_BROKER "broker"
enum {GpioModeInput,GpioPullNo,GpioSpeedLow,SubGhzRadioBrokerStateInitialized};
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
bool subghz_devices_set_channel(const SubGhzDevice*d,uint8_t c) {(void)d;(void)c;return scenario!=5;}
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
typedef struct {bool worker_running,thread_started;void *stream_tx,*stream_rx,*thread;uint32_t frequency;const SubGhzDevice* device;} SubGhzTxRxWorker;
#define furi_check assert
static unsigned threads;
void furi_stream_buffer_reset(void* p) {(void)p;}
bool furi_hal_subghz_is_tx_allowed(uint32_t f) {(void)f;return false;}
void furi_thread_start(void* p) {(void)p;threads++;}
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
