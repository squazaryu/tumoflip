"""Native contracts for device cards, retained versions and sensor hypotheses."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import re
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "applications/system/device_library"
WORKBENCH = ROOT / "applications_user/signal_workbench"


class DeviceLibrarySensorTest(unittest.TestCase):
    def native(self, program, source, directory):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(program)
            run = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(directory), str(path / "test.c"),
                str(directory / source), "-lm", "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            run = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_library_bounds_and_retention(self):
        self.native(r'''
#include "library_model.h"
#include <assert.h>
#include <string.h>
int main(void){
 DeviceCard c={.name="Weather station",.notes="Own sensor",.tags="home outdoor"};
 assert(library_card_valid(&c));assert(library_name_valid("AC-01"));
 assert(!library_name_valid("../a"));assert(!library_name_valid(""));
 assert(!library_name_valid(NULL));assert(!library_name_valid(" bad"));assert(!library_name_valid("bad "));assert(!library_name_valid("abcdefghijklmnopqrstuvwxyz"));
 assert(library_path_valid("/ext/subghz/Test.sub"));
 assert(!library_path_valid("/ext/subghz/../nfc/Test.nfc"));
 assert(!library_path_valid("/int/nfc/Test.nfc"));
 assert(!library_path_valid("/ext/a//b"));
 assert(!library_path_valid("/ext/a\nb"));
 assert(!library_path_valid(NULL));assert(!library_path_valid("/ext/"));assert(!library_path_valid("/ext/a/"));assert(!library_path_valid("/ext/a/."));assert(!library_path_valid("/ext/a/.."));assert(!library_path_valid("/ext/a\\b"));assert(!library_path_valid("/ext/a/./b"));
 assert(!library_card_valid(NULL));
 strcpy(c.links[0],"/ext/subghz/Test.sub");c.link_count=1;assert(library_card_valid(&c));
 c.link_count=9;assert(!library_card_valid(&c));c.link_count=1;
 memset(c.notes,'x',sizeof(c.notes));assert(!library_card_valid(&c));
 memset(c.notes,0,sizeof(c.notes));strcpy(c.tags,"bad\ntag");assert(!library_card_valid(&c));c.tags[0]=0;
 memset(c.name,'x',sizeof(c.name));assert(!library_card_valid(&c));strcpy(c.name,"Sensor");
 memset(c.links[0],'x',256);assert(!library_card_valid(&c));strcpy(c.links[0],"/ext/a");strcpy(c.links[1],"/ext/a");c.link_count=2;assert(!library_card_valid(&c));
 strcpy(c.links[1],"/int/file");assert(!library_card_valid(&c));
 char long_path[300];memset(long_path,'a',sizeof(long_path));memcpy(long_path,"/ext/",5);long_path[299]=0;assert(!library_path_valid(long_path));
 uint32_t gen[4]={1,3,2,0};assert(library_newest_slot(gen)==1);assert(library_write_slot(gen)==3);
 gen[3]=4;assert(library_newest_slot(gen)==3);assert(library_write_slot(gen)==0);
 memset(gen,0,sizeof(gen));assert(library_newest_slot(gen)==-1);assert(library_write_slot(gen)==0);
 return 0;
}''', "library_model.c", LIBRARY)

    def test_history_and_cards_with_real_files_and_fault_injection(self):
        fixtures = ROOT / "tools/tumoflip/fixtures"
        sources = [LIBRARY / "library_model.h", ROOT / "lib/toolbox/file_history.h",
                   LIBRARY / "library_model.c", LIBRARY / "card_store.h",
                   LIBRARY / "card_store.c", LIBRARY / "history_engine.c"]
        body = '#include "library_io_stubs.h"\n'
        for source in sources:
            body += re.sub(r"^#(?:include|pragma).*\n", "", source.read_text(), flags=re.M) + "\n"
        body += (fixtures / "library_io_host.c").read_text()
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(body)
            (path / "sd").mkdir()
            build = subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra",
                "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"',
                "-I", str(fixtures), "-I", str(ROOT / "lib/mbedtls/include"),
                str(path / "test.c"), str(ROOT / "lib/mbedtls/library/sha256.c"),
                str(ROOT / "lib/mbedtls/library/platform_util.c"), "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(path / "test"), str(path / "sd")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_sensor_training_and_held_out_validation(self):
        self.native(r'''
#include "sensor_fit.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
static void put(SensorObservation*s,unsigned n){s->bits[0]=0xA5;s->bits[1]=n>>8;s->bits[2]=n;memset(s->known,255,sizeof(s->known));}
int main(void){SensorObservation samples[4]={0};SensorFitResult r;
 int values[]={101,153,227,319};
 for(unsigned i=0;i<4;i++){put(&samples[i],values[i]);samples[i].measured10=values[i];}
 assert(sensor_fit(samples,24,&r));assert(r.count>0);
 bool found=false;for(unsigned i=0;i<r.count;i++)if(r.candidates[i].start==8&&r.candidates[i].width==16&&!r.candidates[i].little_endian&&!r.candidates[i].signed_value&&r.candidates[i].scale10==1&&r.candidates[i].offset10==0){assert(r.candidates[i].holdout_match);found=true;}
 assert(found);
 samples[3].measured10=400;assert(sensor_fit(samples,24,&r));
 for(unsigned i=0;i<r.count;i++)assert(!r.candidates[i].holdout_match);
 for(unsigned i=0;i<3;i++)samples[i].measured10=1;assert(!sensor_fit(samples,24,&r));
 assert(!sensor_fit(NULL,24,&r));assert(!sensor_fit(samples,0,&r));
 for(unsigned i=0;i<4;i++){put(&samples[i],(uint16_t)(int16_t)(-200+(int)i*137));samples[i].measured10=-200+(int)i*137;}
 assert(sensor_fit(samples,24,&r));found=false;
 for(unsigned i=0;i<r.count;i++)if(r.candidates[i].start==8&&r.candidates[i].width==16&&r.candidates[i].signed_value&&r.candidates[i].scale10==1&&r.candidates[i].offset10==0){assert(r.candidates[i].holdout_match);found=true;}
 assert(found);
 memset(samples,0,sizeof(samples));assert(!sensor_fit(samples,24,&r));
 for(unsigned i=0;i<4;i++)samples[i].measured10=i*10;assert(!sensor_fit(samples,24,&r));
 samples[0].measured10=1000001;assert(!sensor_fit(samples,24,&r));assert(!sensor_fit(samples,97,&r));assert(!sensor_fit(samples,24,NULL));
 for(unsigned i=0;i<4;i++){put(&samples[i],101+i*31);samples[i].measured10=(101+i*31)*10-5;}
 assert(sensor_fit(samples,24,&r));found=false;for(unsigned i=0;i<r.count;i++)if(r.candidates[i].scale10==10&&r.candidates[i].offset10==-5&&r.candidates[i].holdout_match)found=true;assert(found);
 for(unsigned i=0;i<4;i++){unsigned v=6011+i*137;put(&samples[i],v);samples[i].bits[1]=v;samples[i].bits[2]=v>>8;samples[i].measured10=v;}
 assert(sensor_fit(samples,24,&r));found=false;for(unsigned i=0;i<r.count;i++)if(r.candidates[i].little_endian&&r.candidates[i].holdout_match)found=true;assert(found);
 char description[192];SensorCandidate c={.start=8,.width=16,.scale10=1,.predicted10=319,.holdout_match=true};
 assert(sensor_candidate_text(&c,1,319,description,sizeof(description)));assert(strstr(description,"#1 Check: MATCH")&&strstr(description,"31.9 / 31.9"));
 c.holdout_match=false;c.predicted10=-5;c.offset10=-1;c.signed_value=true;
 assert(sensor_candidate_text(&c,2,319,description,sizeof(description)));assert(strstr(description,"FAILED")&&strstr(description,"-0.5")&&strstr(description,"-0.1"));
 assert(!sensor_candidate_text(NULL,1,0,description,sizeof(description)));assert(!sensor_candidate_text(&c,1,0,description,1));
 return 0;}
''', "sensor_fit.c", WORKBENCH)

    def test_sensor_decodes_real_pulse_pairs_and_rejects_truncated_sets(self):
        self.native(r'''
#include "tumospectrum_inference.h"
#include <assert.h>
#include <string.h>
static size_t host_copy(char*d,const char*s,size_t n){size_t len=strlen(s);if(n){size_t k=len<n?len:n-1;memcpy(d,s,k);d[k]=0;}return len;}
#define strlcpy host_copy
#include "tumospectrum_analysis.c"
static void capture(TumoSpectrumCapture*c,unsigned value){
 memset(c,0,sizeof(*c));c->status=TumoSpectrumStatusOk;c->type=TumoSpectrumCaptureSubGhzRaw;c->frequency_hz=433920000;strcpy(c->preset,"AM650");
 unsigned bits=0xA50000|value;
 for(unsigned i=0;i<24;i++){bool one=(bits>>(23-i))&1;c->timings[c->timing_count++]=one?1050:350;c->timings[c->timing_count++]=one?-350:-1050;}
 c->timings[c->timing_count++]=-10000;tumospectrum_analyze(c);
}
int main(void){TumoSpectrumCaptureSet s={.type=TumoSpectrumCaptureSubGhzRaw,.sample_count=4};
 for(unsigned i=0;i<4;i++)capture(&s.samples[i],101+37*i);
 SensorObservation observations[4]={0};observations[0].measured10=999;uint8_t bits=0;
 assert(tumospectrum_sensor_decode(&s,observations,&bits));assert(bits==24&&observations[0].bits[0]==0xA5&&observations[0].bits[2]==101&&observations[0].measured10==999);
 s.samples[3].truncated=true;assert(!tumospectrum_sensor_decode(&s,observations,&bits));s.samples[3].truncated=false;
 s.samples[3].frequency_hz++;assert(!tumospectrum_sensor_decode(&s,observations,&bits));s.samples[3].frequency_hz--;
 strcpy(s.samples[3].preset,"FM238");assert(!tumospectrum_sensor_decode(&s,observations,&bits));strcpy(s.samples[3].preset,"AM650");
 s.sample_count=3;assert(!tumospectrum_sensor_decode(&s,observations,&bits));
 assert(!tumospectrum_sensor_decode(NULL,observations,&bits));return 0;}
''', "tumospectrum_inference.c", WORKBENCH)

    def test_history_gate_precedes_destructive_writes(self):
        for file, start, destructive in (
            ("applications/main/subghz/subghz_i.c", "bool subghz_save_protocol_to_file(", "storage_simply_remove(storage, dev_file_name)"),
            ("applications/main/infrared/infrared_remote.c", "static InfraredErrorCode infrared_remote_batch_start(", "storage_common_rename(storage, path_out, path_in)"),
            ("lib/nfc/nfc_device.c", "bool nfc_device_save(", "flipper_format_buffered_file_open_always(ff, path)"),
        ):
            source = (ROOT / file).read_text().split(start, 1)[1]
            self.assertLess(source.index("file_history_before_write("), source.index(destructive))

    def test_history_requires_checked_sync_and_never_restores_over_original(self):
        source = (LIBRARY / "history_engine.c").read_text()
        for marker in ("FSOM_CREATE_NEW", "storage_file_sync", "memcmp", "mbedtls_sha256", "library_write_slot", "restored_"):
            self.assertIn(marker, source)
        self.assertNotIn("FSOM_CREATE_ALWAYS", source)
        self.assertNotIn("storage_common_rename", source)

    def test_sensor_adapter_uses_training_capture_not_holdout_for_decode_profile(self):
        source = (WORKBENCH / "tumospectrum_inference.c").read_text()
        adapter = source.split("bool tumospectrum_sensor_decode(", 1)[1]
        self.assertIn("captures[0]", adapter)
        self.assertIn("capture->truncated", adapter)
        self.assertIn("sample_count != 4", adapter)
        self.assertIn("tumospectrum_inference_decode_bits", adapter)
        app = (WORKBENCH / "signal_workbench.c").read_text()
        self.assertIn("Sensor Helper", app)
        self.assertIn("sensor_values", app)
        self.assertIn("TumoSpectrumViewSensorInput", app)

    def test_nfc_card_open_inspects_without_automatic_emulation(self):
        source = (ROOT / "applications/main/nfc/nfc_app.c").read_text()
        self.assertIn('"inspect:"', source)
        helper = function(source, "static void nfc_show_initial_scene_for_device(")
        native(r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
typedef int NfcProtocol;
typedef struct{void*nfc_device;void*nfc_supported_cards;void*scene_manager;}NfcApp;
enum{NfcProtocolFeatureEmulateFull=1,NfcProtocolFeatureEmulateUid=2,NfcSceneEmulate=3,NfcSceneSavedMenu=4,DolphinDeedNfcEmulate=5};
static int scene,deeds,cache;
static int nfc_device_get_protocol(void*p){(void)p;return 0;}
static bool nfc_protocol_support_has_feature(int p,NfcApp*a,int f){(void)p;(void)a;(void)f;return true;}
static void nfc_show_loading_popup(NfcApp*a,bool b){(void)a;(void)b;}
static void nfc_supported_cards_load_cache(void*p){(void)p;cache++;}
static void dolphin_deed(int d){assert(d==DolphinDeedNfcEmulate);deeds++;}
static void scene_manager_next_scene(void*p,int s){(void)p;scene=s;}
''' + helper + r'''
int main(void){NfcApp a={0};nfc_show_initial_scene_for_device(&a,true);assert(scene==NfcSceneSavedMenu&&deeds==0&&cache==1);nfc_show_initial_scene_for_device(&a,false);assert(scene==NfcSceneEmulate&&deeds==1);return 0;}
''')

    def test_subghz_short_write_is_not_reported_as_saved(self):
        body = function((ROOT / "applications/main/subghz/subghz_i.c").read_text(), "bool subghz_save_protocol_to_file(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int Storage;typedef int Stream;typedef int FlipperFormat;typedef int FuriString;typedef struct{void*dialogs;}SubGhz;
#define furi_assert assert
#define RECORD_STORAGE "storage"
enum{FSE_OK,FSOM_CREATE_ALWAYS,StreamOffsetFromStart};
static int storage,stream,text;static bool checkpoint=true;static size_t written=6;static int removed;
static Storage*furi_record_open(const char*n){(void)n;return &storage;}
static void furi_record_close(const char*n){(void)n;}
static Stream*flipper_format_get_raw_stream(FlipperFormat*f){(void)f;return &stream;}
static FuriString*furi_string_alloc(void){return &text;}
static void furi_string_free(FuriString*f){(void)f;}
static const char*furi_string_get_cstr(FuriString*f){(void)f;return "/ext/subghz";}
static void path_extract_dirname(const char*p,FuriString*s){(void)p;(void)s;}
static bool flipper_format_delete_key(FlipperFormat*f,const char*k){(void)f;(void)k;return true;}
static bool storage_simply_mkdir(Storage*s,const char*p){(void)s;(void)p;return true;}
static void dialog_message_show_storage_error(void*d,const char*m){(void)d;(void)m;}
static bool file_history_before_write(Storage*s,const char*p){(void)s;(void)p;return checkpoint;}
static bool storage_simply_remove(Storage*s,const char*p){(void)s;(void)p;removed++;return true;}
static bool stream_seek(Stream*s,int p,int mode){(void)s;(void)p;(void)mode;return true;}
static size_t __attribute__((unused)) stream_size(Stream*s){(void)s;return 6;}
static size_t stream_save_to_file(Stream*s,Storage*d,const char*p,int mode){(void)s;(void)d;(void)p;(void)mode;return written;}
static int storage_common_stat(Storage*s,const char*p,void*i){(void)s;(void)p;(void)i;return FSE_OK;}
''' + body + r'''
int main(void){SubGhz s={0};FlipperFormat f=0;assert(subghz_save_protocol_to_file(&s,&f,"/ext/subghz/Test.sub"));written=2;assert(!subghz_save_protocol_to_file(&s,&f,"/ext/subghz/Test.sub"));checkpoint=false;removed=0;assert(!subghz_save_protocol_to_file(&s,&f,"/ext/subghz/Test.sub")&&removed==0);return 0;}
''')


if __name__ == "__main__":
    unittest.main()
