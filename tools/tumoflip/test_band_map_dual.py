"""Execute the real Band Map against deterministic fake radio/broker drivers."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/signal_workbench"


class BandMapDualTests(unittest.TestCase):
    def test_dual_sweep_peak_history_loss_and_broker_release(self):
        source = "\n".join(re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / name).read_text(), flags=re.M)
                           for name in ("tumospectrum_band_map.h", "tumospectrum_band_map.c"))
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define COUNT_OF(x) (sizeof(x)/sizeof(*(x)))
#define RECORD_SUBGHZ_RADIO_BROKER "radio"
typedef struct {int id;}SubGhzDevice;
typedef int SubGhzRadioBroker;
typedef unsigned SubGhzRadioBrokerLease;
enum{SubGhzRadioBrokerStateProbing,SubGhzRadioBrokerStateInitialized,SubGhzRadioBrokerStateCleaningUp,SubGhzRadioBrokerStateRx};
enum{SubGhzRadioBrokerDeviceInternal,SubGhzRadioBrokerDeviceExternalCC1101,SubGhzRadioBrokerDeviceDual};
enum{FuriHalSubGhzPresetOok650Async};
static SubGhzDevice internal={0},external={1};static SubGhzRadioBroker broker;
static bool connected=true,acquired,deny,powered,fail_begin,fail_tune;static unsigned delays,ends[2],reads[2],selected;static uint32_t frequencies[2];
static void*furi_record_open(const char*n){(void)n;return &broker;}
static void furi_record_close(const char*n){(void)n;}
static bool subghz_radio_broker_acquire(SubGhzRadioBroker*b,const char*o,unsigned t,unsigned*l){(void)b;(void)o;(void)t;if(deny)return false;assert(!acquired);acquired=true;*l=1;return true;}
static void subghz_radio_broker_release(SubGhzRadioBroker*b,unsigned*l){(void)b;(void)l;assert(acquired);acquired=false;powered=false;}
static void subghz_radio_broker_set_state(SubGhzRadioBroker*b,unsigned*l,int s){(void)b;(void)l;(void)s;assert(acquired);}
static void subghz_radio_broker_set_selected_device(SubGhzRadioBroker*b,unsigned*l,int d){(void)b;(void)l;assert(acquired);selected=d;}
static bool subghz_radio_broker_external_power_on(SubGhzRadioBroker*b,unsigned*l){(void)b;(void)l;assert(acquired);powered=true;return true;}
static void subghz_radio_broker_external_power_off(SubGhzRadioBroker*b,unsigned*l){(void)b;(void)l;assert(acquired);powered=false;}
static void subghz_devices_init(void){}static void subghz_devices_deinit(void){}
static const SubGhzDevice*subghz_devices_get_by_name(const char*n){return !strcmp(n,"cc1101_int")?&internal:&external;}
static bool subghz_devices_is_connect(const SubGhzDevice*d){assert(d->id==1);return connected;}
static bool subghz_devices_begin(const SubGhzDevice*d){assert(d->id==1&&powered);return !fail_begin;}
static void subghz_devices_end(const SubGhzDevice*d){ends[d->id]++;}
static void subghz_devices_reset(const SubGhzDevice*d){(void)d;assert(acquired);}
static void subghz_devices_load_preset(const SubGhzDevice*d,int p,void*x){(void)d;(void)p;(void)x;assert(acquired);}
static void subghz_devices_idle(const SubGhzDevice*d){(void)d;assert(acquired);}
static void subghz_devices_sleep(const SubGhzDevice*d){(void)d;assert(acquired);}
static bool subghz_devices_is_frequency_valid(const SubGhzDevice*d,uint32_t f){(void)d;return f>=280000000&&f<=960000000;}
static uint32_t subghz_devices_set_frequency(const SubGhzDevice*d,uint32_t f){assert(acquired);frequencies[d->id]=f;return fail_tune?0:f;}
static void subghz_devices_set_rx(const SubGhzDevice*d){(void)d;assert(acquired);}
static float subghz_devices_get_rssi(const SubGhzDevice*d){reads[d->id]++;return frequencies[d->id]>=433850000&&frequencies[d->id]<=433950000?-35.0f:-120.0f;}
static void furi_delay_ms(unsigned n){assert(n==1);delays++;}
''' + source + r'''
int main(void){
 TumoSpectrumBandMap*m=tumospectrum_band_map_alloc();assert(m);
 assert(tumospectrum_band_map_start(m,false));assert(selected==SubGhzRadioBrokerDeviceInternal);
 assert(tumospectrum_band_map_cycle_radio(m));assert(selected==SubGhzRadioBrokerDeviceExternalCC1101);
 assert(tumospectrum_band_map_cycle_radio(m));assert(selected==SubGhzRadioBrokerDeviceDual&&powered);
 TumoSpectrumBandMapSnapshot s;delays=0;reads[0]=reads[1]=0;
 for(unsigned i=0;i<8;i++)tumospectrum_band_map_tick(m);
 tumospectrum_band_map_get_snapshot(m,&s);
 assert(s.radio==TumoSpectrumBandMapRadioDual&&reads[0]==32&&reads[1]==32&&delays==32);
 assert(s.peak_count>0&&s.peak_count<=16);
 assert(tumospectrum_band_map_select_peak(m,0));assert(!tumospectrum_band_map_select_peak(m,16));
 tumospectrum_band_map_toggle_hold(m);unsigned before=delays;tumospectrum_band_map_tick(m);assert(before==delays);tumospectrum_band_map_toggle_hold(m);
 connected=false;tumospectrum_band_map_tick(m);tumospectrum_band_map_get_snapshot(m,&s);
 assert(s.radio==TumoSpectrumBandMapRadioInternal&&!powered&&ends[1]>0);
 tumospectrum_band_map_stop(m);assert(!acquired);tumospectrum_band_map_stop(m);assert(!acquired);
 deny=true;assert(!tumospectrum_band_map_start(m,true));tumospectrum_band_map_get_snapshot(m,&s);assert(s.status==TumoSpectrumBandMapStatusBusy&&!acquired);deny=false;
 connected=true;fail_begin=true;assert(tumospectrum_band_map_start(m,true));tumospectrum_band_map_get_snapshot(m,&s);assert(s.radio==TumoSpectrumBandMapRadioInternal&&!powered);
 tumospectrum_band_map_stop(m);fail_begin=false;assert(tumospectrum_band_map_start(m,false));
 fail_tune=true;tumospectrum_band_map_tick(m);tumospectrum_band_map_get_snapshot(m,&s);assert(s.status==TumoSpectrumBandMapStatusError&&!acquired);
 tumospectrum_band_map_free(m);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
