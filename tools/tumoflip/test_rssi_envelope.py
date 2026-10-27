"""RSSI envelope evidence on synthetic samples only; no radio or TX."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/signal_workbench"


def stats_source():
    header = (APP / "tumospectrum_envelope_stats.h").as_posix()
    implementation = APP / "tumospectrum_envelope_stats.c"
    return (f'#include "{header}"\n#include <assert.h>\n#include <string.h>\n'
            + (implementation.read_text().replace('#include "tumospectrum_envelope_stats.h"', "")
               if implementation.exists() else "")
            + '\nvoid fill_samples(int16_t* s,size_t n,int16_t v){for(size_t i=0;i<n;i++)s[i]=v;}\n')


class RssiEnvelopeTests(unittest.TestCase):
    def test_full_cc1101_rssi_byte_range_and_weak_pulses_are_representable(self):
        native(stats_source() + r'''
int main(void) {
    int16_t samples[128]; TumoEnvelopeWindow w;
    for(int raw=0;raw<256;raw++) {
        // Independent oracle for the actual INT/EXT HAL formulas.
        float dbm=(float)(raw>=128?raw-256:raw)/2.0f-74.0f;
        int16_t measured=(int16_t)dbm;
        fill_samples(samples,128,measured);
        assert(tumo_envelope_analyze(samples,128,measured,&w));
        assert(w.low_dbm==measured&&w.high_dbm==measured);
    }
    for(unsigned i=0;i<128;i++)samples[i]=i%16<8?-120:-138;
    assert(tumo_envelope_analyze(samples,128,-138,&w));
    assert(w.kind==TumoEnvelopePulsed&&w.low_dbm==-138&&w.high_dbm==-120);
    return 0;
}
''')

    def test_pull_request_and_release_ci_run_the_new_regressions(self):
        for name in ('pr-build.yml', 'release.yml'):
            workflow = (ROOT / '.github/workflows' / name).read_text()
            self.assertTrue('tools.tumoflip.test_rssi_envelope' in workflow,
                            f'{name} does not run the envelope regressions')

    def test_flipper_controls_retry_calibrate_and_release_on_back(self):
        source = (APP / "signal_workbench.c").read_text()
        signatures = (
            "static uint32_t tumospectrum_envelope_previous(",
            "static bool tumospectrum_envelope_input(",
        )
        body = '\n'.join(function(source, s) if s in source else s +
                         ('void* context);' if 'previous' in s else 'InputEvent* event, void* context);')
                         for s in signatures)
        native(f'#include "{APP / "tumospectrum_envelope.h"}"\n' + r'''
#include <assert.h>
#include <stddef.h>
enum {InputTypeShort,InputTypeLong,InputTypeRepeat,InputKeyBack,InputKeyLeft,
      InputKeyRight,InputKeyUp,InputKeyDown,InputKeyOk,TumoSpectrumViewMenu};
typedef struct {unsigned type,key;} InputEvent;
typedef struct {TumoSpectrumEnvelope* envelope;} TumoSpectrumApp;
static unsigned stops,frees,refreshes,tunes,cycles,bands,starts,calibrations;
static TumoEnvelopeSnapshot snap={.status=TumoEnvelopeRunning,.frequency_hz=433920000};
void tumospectrum_envelope_snapshot(const TumoSpectrumEnvelope* e,TumoEnvelopeSnapshot* s){assert(e);*s=snap;}
void tumospectrum_envelope_stop(TumoSpectrumEnvelope* e){assert(e);stops++;}
void tumospectrum_envelope_free(TumoSpectrumEnvelope* e){assert(e);frees++;}
bool tumospectrum_envelope_tune(TumoSpectrumEnvelope* e,int d){assert(e&&(d==1||d==-1));tunes++;return true;}
bool tumospectrum_envelope_cycle_radio(TumoSpectrumEnvelope* e){assert(e);cycles++;return true;}
void tumospectrum_envelope_next_band(TumoSpectrumEnvelope* e){assert(e);bands++;}
bool tumospectrum_envelope_start(TumoSpectrumEnvelope* e,uint32_t f,bool ext){assert(e&&f==433920000&&!ext);starts++;return true;}
void tumospectrum_envelope_calibrate(TumoSpectrumEnvelope* e){assert(e);calibrations++;}
void tumospectrum_refresh_envelope(TumoSpectrumApp* a){assert(a);refreshes++;}
''' + body + r'''
int main(void){
    TumoSpectrumApp app={.envelope=(void*)1};
    const unsigned keys[]={InputKeyLeft,InputKeyRight,InputKeyUp,InputKeyDown,InputKeyOk};
    for(unsigned i=0;i<5;i++){InputEvent e={InputTypeShort,keys[i]};assert(tumospectrum_envelope_input(&e,&app));}
    assert(tunes==2&&cycles==1&&bands==1&&calibrations==1&&refreshes==5);
    snap.status=TumoEnvelopeBusy;InputEvent e={InputTypeShort,InputKeyOk};
    assert(tumospectrum_envelope_input(&e,&app)&&starts==1);
    e=(InputEvent){InputTypeLong,InputKeyOk};assert(!tumospectrum_envelope_input(&e,&app));
    e=(InputEvent){InputTypeShort,InputKeyBack};assert(!tumospectrum_envelope_input(&e,&app));
    assert(tumospectrum_envelope_previous(&app)==TumoSpectrumViewMenu);
    assert(frees==1&&!app.envelope);
    return 0;
}
''')

    def test_app_routes_envelope_without_a_background_service_or_driver_abi(self):
        source = (APP / "signal_workbench.c").read_text()
        for route in ('"Envelope Check"', 'case TumoSpectrumMenuEnvelope:',
                      'tumospectrum_envelope_tick(app->envelope)',
                      'tumospectrum_envelope_free(app->envelope)',
                      'TumoSpectrumViewEnvelope, app->envelope_view',
                      'with_view_model(\n        app->envelope_view'):
            self.assertTrue(route in source, f'Missing envelope route: {route}')
        self.assertIn('fap_version="3.5.0"', (APP / 'application.fam').read_text())
        sampler = (APP / 'tumospectrum_envelope.c').read_text()
        self.assertIn('subghz_devices_get_rssi(envelope->device)', sampler)
        self.assertNotRegex(sampler, r'subghz_devices_(?:set_tx|start_async_tx|write_packet)\s*\(')

    def test_radio_lease_faults_retune_calibration_and_external_disconnect(self):
        implementation = APP / "tumospectrum_envelope.c"
        body = implementation.read_text() if implementation.exists() else ""
        body = re.sub(r'^#include .*\n', '', body, flags=re.M)
        native(stats_source() + f'\n#include "{APP / "tumospectrum_envelope.h"}"\n' + r'''
#include <stdlib.h>
#include <math.h>
#define RECORD_SUBGHZ_RADIO_BROKER "radio"
#define COUNT_OF(x) (sizeof(x)/sizeof(*(x)))
typedef struct {int id;} SubGhzDevice;
typedef int SubGhzRadioBroker;
typedef unsigned SubGhzRadioBrokerLease;
enum {SubGhzRadioBrokerStateProbing,SubGhzRadioBrokerStateInitialized,
      SubGhzRadioBrokerStateRx,SubGhzRadioBrokerStateCleaningUp,
      SubGhzRadioBrokerDeviceInternal,SubGhzRadioBrokerDeviceExternalCC1101,
      FuriHalSubGhzPreset2FSKDev476Async};
static SubGhzDevice radios[2]={{0},{1}};
static int records,leases,inits,ends[2],reads;
static bool deny,connected=true,begin_fail,tune_fail,power_fail,missing,invalid_rssi,slow_read,quiet_limit;
static unsigned elapsed,clock_tick,signal_mode;
static void* furi_record_open(const char*n){assert(!strcmp(n,"radio"));records++;return &records;}
static void furi_record_close(const char*n){assert(!strcmp(n,"radio")&&records>0);records--;}
static bool subghz_radio_broker_acquire(SubGhzRadioBroker*b,const char*n,unsigned t,SubGhzRadioBrokerLease*l){(void)b;assert(n&&t<=500);if(deny)return false;assert(!leases);leases++;*l=1;return true;}
static void subghz_radio_broker_release(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l){(void)b;assert(*l&&leases);leases--;}
static void subghz_radio_broker_set_state(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l,int s){(void)b;(void)s;assert(*l&&leases);}
static void subghz_radio_broker_set_selected_device(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l,int s){(void)b;(void)s;assert(*l&&leases);}
static bool subghz_radio_broker_external_power_on(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l){(void)b;assert(*l&&leases);return !power_fail;}
static void subghz_devices_init(void){assert(leases);inits++;}
static void subghz_devices_deinit(void){assert(inits>0);inits--;}
static const SubGhzDevice* subghz_devices_get_by_name(const char*n){assert(inits);return missing?NULL:&radios[strstr(n,"ext")?1:0];}
static bool subghz_devices_is_connect(const SubGhzDevice*d){assert(d);return d->id==0||connected;}
static bool subghz_devices_begin(const SubGhzDevice*d){assert(d);return !begin_fail;}
static void subghz_devices_end(const SubGhzDevice*d){assert(leases);ends[d->id]++;}
static void subghz_devices_reset(const SubGhzDevice*d){assert(d&&leases);}
static void subghz_devices_load_preset(const SubGhzDevice*d,int p,void*c){assert(d&&leases&&p==FuriHalSubGhzPreset2FSKDev476Async&&!c);}
static bool furi_hal_subghz_is_frequency_valid(uint32_t f){return(f>=281000000&&f<=361000000)||(f>=378000000&&f<=481000000)||(f>=749000000&&f<=962000000);}
static uint32_t subghz_devices_set_frequency(const SubGhzDevice*d,uint32_t f){assert(d&&leases&&furi_hal_subghz_is_frequency_valid(f));return tune_fail?0:f;}
static void subghz_devices_set_rx(const SubGhzDevice*d){assert(d&&leases);}
static void subghz_devices_idle(const SubGhzDevice*d){assert(d&&leases);}
static void subghz_devices_sleep(const SubGhzDevice*d){assert(d&&leases);}
static float subghz_devices_get_rssi(const SubGhzDevice*d){assert(d&&leases);reads++;if(slow_read)elapsed+=2000;if(invalid_rssi)return NAN;if(quiet_limit)return -138;return !signal_mode?-105:signal_mode==1?-60:((reads%16)<8?-60:-105);}
static void furi_delay_ms(unsigned ms){elapsed+=ms*1000;}
static void furi_delay_us(unsigned us){assert(us<=100);elapsed+=us;}
static void furi_thread_yield(void){}
static uint32_t furi_get_tick(void){return clock_tick+elapsed/1000;}
'''.replace('\nstatic ', '\n') + body + r'''
int main(void) {
    assert(!tumospectrum_envelope_start(NULL,433920000,false));
    assert(!tumospectrum_envelope_cycle_radio(NULL));
    assert(!tumospectrum_envelope_tune(NULL,1));
    tumospectrum_envelope_calibrate(NULL);tumospectrum_envelope_tick(NULL);
    tumospectrum_envelope_stop(NULL);tumospectrum_envelope_next_band(NULL);
    tumospectrum_envelope_snapshot(NULL,NULL);
    TumoSpectrumEnvelope* e=tumospectrum_envelope_alloc(); assert(e&&sizeof(*e)<=512);
    assert(!tumospectrum_envelope_tune(e,0));
    TumoEnvelopeSnapshot s;
    deny=true;assert(!tumospectrum_envelope_start(e,433920000,false));
    tumospectrum_envelope_snapshot(e,&s);assert(s.status==TumoEnvelopeBusy&&!records&&!leases&&!inits);deny=false;
    assert(!tumospectrum_envelope_start(e,UINT32_MAX,false));assert(!records&&!leases);
    connected=false;assert(!tumospectrum_envelope_start(e,433920000,true));
    assert(!records&&!leases&&!inits);connected=true;
    begin_fail=true;assert(!tumospectrum_envelope_start(e,433920000,true));assert(ends[1]&&!leases&&!records&&!inits);begin_fail=false;
    missing=true;assert(!tumospectrum_envelope_start(e,433920000,false));assert(!leases&&!records&&!inits);missing=false;
    power_fail=true;assert(!tumospectrum_envelope_start(e,433920000,true));assert(!leases&&!records&&!inits);power_fail=false;
    tune_fail=true;assert(!tumospectrum_envelope_start(e,433920000,false));assert(!leases&&!records&&!inits);tune_fail=false;
    assert(tumospectrum_envelope_start(e,433920000,false));
    quiet_limit=true;tumospectrum_envelope_tick(e);tumospectrum_envelope_snapshot(e,&s);
    int measured_floor=s.floor_dbm;
    assert(s.status==TumoEnvelopeRunning&&s.calibrated&&measured_floor==-138);
    quiet_limit=false;tumospectrum_envelope_calibrate(e);
    clock_tick=UINT32_MAX-5;elapsed=0;
    tumospectrum_envelope_tick(e);tumospectrum_envelope_snapshot(e,&s);
    assert(s.calibrated&&s.floor_dbm==-105&&s.sample_period_us>=100);
    signal_mode=2;
    for(unsigned i=0;i<6;i++)tumospectrum_envelope_tick(e);
    tumospectrum_envelope_snapshot(e,&s);assert(s.kind==TumoEnvelopePulsed&&s.agreement==100);
    assert(tumospectrum_envelope_tune(e,1));tumospectrum_envelope_snapshot(e,&s);
    assert(s.frequency_hz==433945000&&!s.calibrated&&!s.agreement);
    assert(tumospectrum_envelope_cycle_radio(e));tumospectrum_envelope_snapshot(e,&s);assert(s.external);
    connected=false;int before=reads;tumospectrum_envelope_tick(e);assert(reads==before);
    tumospectrum_envelope_snapshot(e,&s);assert(s.status==TumoEnvelopeMissing&&!leases&&!records&&!inits);
    connected=true;signal_mode=0;assert(tumospectrum_envelope_start(e,433920000,false));
    tumospectrum_envelope_calibrate(e);tumospectrum_envelope_tick(e);
    invalid_rssi=true;tumospectrum_envelope_tick(e);invalid_rssi=false;
    tumospectrum_envelope_snapshot(e,&s);assert(s.status==TumoEnvelopeError&&!leases&&!records&&!inits);
    assert(tumospectrum_envelope_start(e,433920000,false));slow_read=true;
    tumospectrum_envelope_tick(e);slow_read=false;
    tumospectrum_envelope_snapshot(e,&s);assert(s.status==TumoEnvelopeError&&!leases&&!records&&!inits);
    assert(tumospectrum_envelope_start(e,361000000,false));
    assert(!tumospectrum_envelope_tune(e,1)); // never call crash-on-invalid HAL
    tumospectrum_envelope_next_band(e);tumospectrum_envelope_snapshot(e,&s);assert(s.frequency_hz==390000000);
    tumospectrum_envelope_stop(e);tumospectrum_envelope_stop(e);
    assert(!leases&&!records&&!inits);before=reads;tumospectrum_envelope_tick(e);assert(reads==before);
    tumospectrum_envelope_free(e);tumospectrum_envelope_free(NULL);
    return 0;
}
''')

    def test_weak_overloaded_flat_pulsed_and_ambiguous_windows(self):
        native(stats_source() + r'''
int main(void) {
    int16_t s[TUMO_ENVELOPE_SAMPLES]; TumoEnvelopeWindow w;
    fill_samples(s, 128, -105);
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopeNoSignal);
    fill_samples(s, 128, -60);
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopeFlat && w.transitions == 0);
    for (unsigned i = 0; i < 128; ++i) s[i] = i % 16 < 8 ? -60 : -105;
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopePulsed && w.transitions >= 4);
    for (unsigned i = 0; i < 128; ++i) s[i] = i % 16 < 8 ? -60 : -83;
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopeUnclear); // no quiet-floor evidence
    for (unsigned i = 0; i < 128; ++i) s[i] = i % 16 < 8 ? -20 : -105;
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopeTooStrong);
    fill_samples(s, 128, -105); s[40] = -50;
    assert(tumo_envelope_analyze(s, 128, -105, &w));
    assert(w.kind == TumoEnvelopeNoSignal); // isolated noise spike
    return 0;
}
''')

    def test_invalid_inputs_do_not_read_past_bounds_or_reuse_a_verdict(self):
        native(stats_source() + r'''
int main(void) {
    int16_t s[TUMO_ENVELOPE_SAMPLES] = {0};
    TumoEnvelopeWindow w = {.kind = TumoEnvelopePulsed};
    assert(!tumo_envelope_analyze(NULL, 128, -100, &w));
    assert(w.kind == TumoEnvelopeInvalid);
    assert(!tumo_envelope_analyze(s, 0, -100, &w));
    assert(!tumo_envelope_analyze(s, 15, -100, &w));
    assert(!tumo_envelope_analyze(s, 129, -100, &w));
    assert(!tumo_envelope_analyze(s, 128, -100, NULL));
    assert(!tumo_envelope_analyze(s, 128, -139, &w));
    assert(!tumo_envelope_analyze(s, 128, -100, &w)); // impossible RSSI=0
    fill_samples(s, 128, -60); s[127] = -139;
    assert(!tumo_envelope_analyze(s, 128, -100, &w));
    return 0;
}
''')

    def test_bounded_rolling_agreement_never_keeps_an_old_signal(self):
        native(stats_source() + r'''
int main(void) {
    TumoEnvelopeSession s; TumoEnvelopeWindow w = {.kind = TumoEnvelopePulsed};
    tumo_envelope_session_reset(&s);
    for (unsigned i=0; i<5; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeLearning && s.agreement == 0);
    tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopePulsed && s.agreement == 100);
    w.kind = TumoEnvelopeFlat;
    for (unsigned i=0; i<6; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeUnclear && s.agreement == 0);
    for (unsigned i=0; i<100000; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.count == TUMO_ENVELOPE_HISTORY && s.kind == TumoEnvelopeFlat);
    assert(s.agreement == 100);
    w.kind = TumoEnvelopeNoSignal; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeNoSignal && !s.agreement && !s.count);
    w.kind = TumoEnvelopeTooStrong; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeTooStrong && !s.agreement && !s.count);
    w.kind = TumoEnvelopePulsed; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeLearning && !s.agreement);
    assert(!strcmp(tumo_envelope_kind_name(TumoEnvelopeFlat), "Flat carrier"));
    for(unsigned i=0;i<=TumoEnvelopeInvalid;i++)assert(tumo_envelope_kind_name(i)[0]);
    tumo_envelope_session_reset(NULL);tumo_envelope_session_add(NULL,&w);
    tumo_envelope_session_add(&s,NULL);
    return 0;
}
''')

    def test_random_windows_are_read_only_and_bounded(self):
        native(stats_source() + r'''
int main(void) {
    int16_t samples[128], original[128]; uint32_t rng=0x12345678;
    TumoEnvelopeWindow w;
    for (unsigned trial=0; trial<10000; ++trial) {
        for (unsigned i=0; i<128; ++i) {
            rng = rng * 1664525U + 1013904223U;
            samples[i] = -(int16_t)(1 + (rng % 138));
        }
        memcpy(original, samples, sizeof(samples));
        assert(tumo_envelope_analyze(samples, 128, -105, &w));
        assert(!memcmp(samples, original, sizeof(samples)));
        assert(w.low_dbm <= w.high_dbm && w.transitions < 128);
        assert(w.kind >= TumoEnvelopeNoSignal && w.kind <= TumoEnvelopeTooStrong);
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
