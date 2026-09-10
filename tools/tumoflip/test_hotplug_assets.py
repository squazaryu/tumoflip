"""Execute production hotplug and asset-overlay functions against host fixtures."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    return source[start:end]


def run_c(body):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(body)
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Werror", str(path / "test.c"), "-o", str(path / "test")],
            check=True, capture_output=True, text=True,
        )
        subprocess.run([str(path / "test")], check=True, capture_output=True, text=True)


class HotplugAssetsTests(unittest.TestCase):
    def test_external_tx_timeout_releases_bus_and_disables_amplifier(self):
        source = (ROOT / "applications/drivers/subghz/cc1101_ext/cc1101_ext.c").read_text()
        production = function(source, "bool subghz_device_cc1101_ext_tx(")
        run_c(r"""
#include <stdbool.h>
#include <assert.h>
enum { SubGhzDeviceCC1101ExtRegulationTxRx=1,CC1101StateTX=2,
 SUBGHZ_DEVICE_CC1101_EXT_E07_AMP_GPIO=3,CC1101_IOCFG2=4,CC1101IocfgHW=5 };
static struct { int regulation;bool amp_and_leds;void* spi_bus_handle; } device;
static __typeof__(device)* subghz_device_cc1101_ext=&device;
static bool locked, reached_tx, amplifier;
static int writes;
static void furi_hal_spi_acquire(void* h) { (void)h;assert(!locked);locked=true; }
static void furi_hal_spi_release(void* h) { (void)h;assert(locked);locked=false; }
static void cc1101_switch_to_tx(void* h) { (void)h;assert(locked); }
static bool cc1101_wait_status_state(void* h,int s,int t) {
 (void)h;assert(locked && s==CC1101StateTX && t==10000);return reached_tx;
}
static void furi_hal_gpio_write(int p,bool v) { (void)p;amplifier=v; }
static void cc1101_write_reg(void* h,int r,int v) { (void)h;(void)r;(void)v;writes++; }
""" + production + r"""
int main(void) {
 device.regulation=SubGhzDeviceCC1101ExtRegulationTxRx;device.amp_and_leds=true;
 reached_tx=true;assert(subghz_device_cc1101_ext_tx());assert(amplifier && !locked && writes==1);
 reached_tx=false;assert(!subghz_device_cc1101_ext_tx());assert(!amplifier && !locked && writes==1);
 device.regulation=0;assert(!subghz_device_cc1101_ext_tx());assert(!locked);
 return 0;
}
""")

    def test_standard_and_arf_hotplug_state_transitions(self):
        for app in ("applications/main/subghz", "applications_user/arf_subghz_full"):
            with self.subTest(app=app):
                source = (ROOT / app / "helpers/subghz_txrx.c").read_text()
                production = function(source, "bool subghz_txrx_radio_device_poll_active(")
                production += "\n" + function(source, "void subghz_txrx_radio_device_poll_reacquire(")
                run_c(r"""
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
enum { SubGhzRadioDeviceTypeInternal, SubGhzRadioDeviceTypeExternalCC1101, SubGhzRadioDeviceTypeAuto };
enum { SubGhzTxRxStateIDLE, SubGhzTxRxStateSleep, SubGhzTxRxStateRx, SubGhzTxRxStateTx };
enum { SubGhzProtocolFlag_RAW = 8 };
typedef struct { const char* name; uint32_t frequency; } Preset;
typedef struct { int radio_device_type, preferred_radio_device_type, txrx_state;
 uint32_t radio_device_probe_tick; int receiver_filter;
 void *worker, *radio_device, *setting; Preset* preset; } SubGhzTxRx;
static bool connected, running;
static int stops, switches, resumes, resets, probes, reads;
static uint32_t tick;
static bool subghz_worker_is_running(void* w) { (void)w; return running; }
static bool subghz_devices_is_connect(void* d) { (void)d; reads++; return connected; }
static void subghz_txrx_stop(SubGhzTxRx* s) { stops++; running=false; s->txrx_state=SubGhzTxRxStateIDLE; }
static void subghz_txrx_radio_device_fallback_internal(SubGhzTxRx* s) {
 assert(!running); switches++; s->radio_device_type=SubGhzRadioDeviceTypeInternal; s->radio_device_probe_tick=tick;
}
static void subghz_txrx_receiver_reset(SubGhzTxRx* s) { (void)s; resets++; }
static const char* furi_string_get_cstr(const char* s) { return s; }
static void* subghz_setting_get_preset_data_by_name(void* s,const char* n) { (void)n; return s; }
static void subghz_txrx_begin(SubGhzTxRx* s,void* p) { (void)s;(void)p; assert(!running); }
static void subghz_txrx_rx(SubGhzTxRx* s,uint32_t f) { assert(f==433920000);resumes++;running=true;s->txrx_state=SubGhzTxRxStateRx; }
static uint32_t furi_get_tick(void) { return tick; }
static uint32_t furi_ms_to_ticks(uint32_t t) { return t; }
static void subghz_txrx_radio_device_reprobe_preferred(SubGhzTxRx* s) {
 probes++;s->radio_device_probe_tick=tick;
 if(connected)s->radio_device_type=s->preferred_radio_device_type;
}
""" + production + r"""
int main(void) {
 Preset p={"AM650",433920000};
 SubGhzTxRx s={.preset=&p,.radio_device_type=SubGhzRadioDeviceTypeExternalCC1101,
 .preferred_radio_device_type=SubGhzRadioDeviceTypeAuto,.txrx_state=SubGhzTxRxStateRx};
 connected=true;running=true;
 assert(!subghz_txrx_radio_device_poll_active(&s));assert(stops==0);
 connected=false;
 assert(subghz_txrx_radio_device_poll_active(&s));
 assert(stops==1 && switches==1 && resumes==1 && resets==1);
 assert(s.preferred_radio_device_type==SubGhzRadioDeviceTypeAuto);
 // Internal never probes the SPI bus.
 int read_count=reads; assert(!subghz_txrx_radio_device_poll_active(&s));assert(reads==read_count);
 // RAW pending samples survive a radio switch.
 s.radio_device_type=SubGhzRadioDeviceTypeAuto;s.receiver_filter=SubGhzProtocolFlag_RAW;
 assert(subghz_txrx_radio_device_poll_active(&s));assert(resets==1 && resumes==2);
 // TX and synchronous analyzer ownership are never torn down by a receiver tick.
 s.radio_device_type=SubGhzRadioDeviceTypeExternalCC1101;s.txrx_state=SubGhzTxRxStateTx;
 assert(!subghz_txrx_radio_device_poll_active(&s));assert(stops==2);
 s.txrx_state=SubGhzTxRxStateRx;running=false;
 assert(!subghz_txrx_radio_device_poll_active(&s));assert(stops==2);
 // Menu recovery waits 5 seconds, restores Auto and handles tick wrap.
 s.radio_device_type=SubGhzRadioDeviceTypeInternal;s.txrx_state=SubGhzTxRxStateIDLE;
 tick=4999;subghz_txrx_radio_device_poll_reacquire(&s);assert(probes==0);
 tick=5000;connected=true;subghz_txrx_radio_device_poll_reacquire(&s);
 assert(probes==1 && s.radio_device_type==SubGhzRadioDeviceTypeAuto);
 s.radio_device_type=SubGhzRadioDeviceTypeInternal;s.preferred_radio_device_type=SubGhzRadioDeviceTypeInternal;
 tick=15000;subghz_txrx_radio_device_poll_reacquire(&s);assert(probes==1);
 s.preferred_radio_device_type=SubGhzRadioDeviceTypeExternalCC1101;s.radio_device_probe_tick=UINT32_MAX-1000;
 tick=4000;subghz_txrx_radio_device_poll_reacquire(&s);assert(probes==2);
 return 0;
}
""")

    def test_overlay_is_removed_on_completion_failure_and_reentry(self):
        source = (ROOT / "applications/services/loader/loader.c").read_text()
        production = function(source, "static void loader_assets_progress(")
        production += "\n" + function(source, "static void loader_assets_progress_finish(")
        run_c(r"""
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef struct { bool assets_loading_visible;void *loading,*view_holder; } Loader;
static void* shown;
static float value;
static int attaches,detaches;
static void loading_reset_progress(void* l) { (void)l;value=0; }
static void loading_set_progress(void* l,float p) { (void)l;value=p; }
static void* loading_get_view(void* l) { return l; }
static void view_holder_set_view(void* h,void* v) { (void)h;shown=v;if(v)attaches++;else detaches++; }
static void view_holder_send_to_front(void* h) { (void)h; }
""" + production + r"""
int main(void) {
 Loader l={.loading=(void*)1};
 loader_assets_progress(&l,0,0);assert(!shown);
 loader_assets_progress(&l,0,4);assert(shown && value==0);
 loader_assets_progress(&l,2,4);assert(value==0.5f && attaches==1);
 loader_assets_progress(&l,4,4);loader_assets_progress_finish(&l);
 assert(!shown && !l.assets_loading_visible && detaches==1 && value==0);
 loader_assets_progress(&l,0,10);assert(shown && value==0 && attaches==2);
 // Failed preload uses the same finish operation.
 loader_assets_progress_finish(&l);loader_assets_progress_finish(&l);
 assert(!shown && detaches==2);
 return 0;
}
""")
        start = function(source, "static LoaderMessageLoaderStatusResult loader_start_external_app(")
        self.assertLess(start.index("loader_assets_progress_finish(loader)"), start.index("if(preload_res"))
        self.assertLess(start.index("loader_assets_progress_finish(loader)"), start.index("loader_start_app_thread"))


if __name__ == "__main__":
    unittest.main()
