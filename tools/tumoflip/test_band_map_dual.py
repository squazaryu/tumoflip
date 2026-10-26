"""Execute the real Band Map against deterministic fake radio/broker drivers."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/signal_workbench"


class BandMapDualTests(unittest.TestCase):
    def test_peak_menu_uses_bounded_native_rows_and_keeps_an_empty_state(self):
        header = re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / "tumospectrum_band_map.h").read_text(), flags=re.M)
        source = (APP / "signal_workbench.c").read_text()
        signature = "static void tumospectrum_open_peak_history("
        body = function(source, signature) if signature in source else "static void tumospectrum_open_peak_history(TumoSpectrumApp* app);"
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
''' + header + r'''
typedef struct{unsigned count;char rows[20][64];}Submenu;
typedef struct{Submenu*peak_menu;TumoSpectrumBandMap*band_map;void*view_dispatcher;}TumoSpectrumApp;
enum{TumoSpectrumViewBandPeaks};
static TumoSpectrumBandMapSnapshot snapshot;
static void tumospectrum_peak_selected(void*c,uint32_t n){(void)c;(void)n;}
void tumospectrum_band_map_get_snapshot(const TumoSpectrumBandMap*m,TumoSpectrumBandMapSnapshot*s){(void)m;*s=snapshot;}
static void submenu_reset(Submenu*m){m->count=0;}
static void submenu_set_header(Submenu*m,const char*h){(void)m;assert(!strcmp(h,"Peak history"));}
static void submenu_add_item(Submenu*m,const char*l,uint32_t n,void(*c)(void*,uint32_t),void*a){(void)n;(void)c;(void)a;assert(m->count<20);snprintf(m->rows[m->count++],64,"%s",l);}
static void view_dispatcher_switch_to_view(void*d,unsigned n){(void)d;assert(n==TumoSpectrumViewBandPeaks);}
''' + body + r'''
int main(void){Submenu menu={0};TumoSpectrumApp app={.peak_menu=&menu};
 tumospectrum_open_peak_history(&app);assert(menu.count>=1&&!strcmp(menu.rows[0],"No peaks yet"));
 snapshot.peak_count=16;for(unsigned i=0;i<16;i++)snapshot.observed_peaks[i]=(TumoSpectrumBandPeak){.frequency_hz=433920000+i*1000,.rssi_dbm=-45};
 tumospectrum_open_peak_history(&app);assert(menu.count==17&&strstr(menu.rows[0],"433.920"));return 0;}
''')

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
