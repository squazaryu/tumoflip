"""Native contracts for device cards, retained versions and sensor hypotheses."""
from pathlib import Path
import subprocess
import tempfile
import unittest

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
 assert(library_path_valid("/ext/subghz/Test.sub"));
 assert(!library_path_valid("/ext/subghz/../nfc/Test.nfc"));
 assert(!library_path_valid("/int/nfc/Test.nfc"));
 assert(!library_path_valid("/ext/a//b"));
 assert(!library_path_valid("/ext/a\nb"));
 strcpy(c.links[0],"/ext/subghz/Test.sub");c.link_count=1;assert(library_card_valid(&c));
 c.link_count=9;assert(!library_card_valid(&c));c.link_count=1;
 memset(c.notes,'x',sizeof(c.notes));assert(!library_card_valid(&c));
 uint32_t gen[4]={1,3,2,0};assert(library_newest_slot(gen)==1);assert(library_write_slot(gen)==3);
 gen[3]=4;assert(library_newest_slot(gen)==3);assert(library_write_slot(gen)==0);
 memset(gen,0,sizeof(gen));assert(library_newest_slot(gen)==-1);assert(library_write_slot(gen)==0);
 return 0;
}''', "library_model.c", LIBRARY)

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
 memset(samples,0,sizeof(samples));assert(!sensor_fit(samples,24,&r));return 0;}
''', "sensor_fit.c", WORKBENCH)

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


if __name__ == "__main__":
    unittest.main()
