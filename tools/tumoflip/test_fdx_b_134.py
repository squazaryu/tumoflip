"""Exercise actual FDX-B encode/decode with weak-tag duty skew and bad input."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_lfrfid_write_fill import source

ROOT = Path(__file__).resolve().parents[2]


class FdxB134Tests(unittest.TestCase):
    def test_134_mode_is_exposed_without_reordering_existing_worker_events(self):
        header = (ROOT / "lib/lfrfid/lfrfid_worker.h").read_text()
        self.assertIn("LFRFIDWorkerReadTypeASK134", header)
        self.assertLess(header.index("LFRFIDWorkerReadDone,"), header.index("LFRFIDWorkerReadStartASK134,"))
        self.assertLess(header.index("LFRFIDWorkerReadTypePSKOnly,"), header.index("LFRFIDWorkerReadTypeASK134"))
        for name in ("applications/main/lfrfid/lfrfid_cli.c",
                     "applications/main/lfrfid/scenes/lfrfid_scene_extra_actions.c",
                     "applications/main/lfrfid/scenes/lfrfid_scene_raw_read.c"):
            self.assertIn("LFRFIDWorkerReadTypeASK134", (ROOT / name).read_text())
        worker = (ROOT / "lib/lfrfid/lfrfid_worker_modes.c").read_text()
        self.assertIn("furi_hal_rfid_tim_read_start(134200, 0.5)", worker)
        self.assertIn("step = (step + 1) % 3", worker)

    def test_actual_decoder_clean_weak_134_crc_and_bad_durations(self):
        text = (ROOT / "lib/lfrfid/protocols/protocol_fdx_b.c").read_text()
        text = text[:text.index("static uint64_t protocol_fdx_b_get_national_code(")]
        text = "\n".join(line for line in text.splitlines() if not line.startswith("#include"))
        body = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define UNUSED(x) (void)(x)
#define furi_check assert
#define furi_assert assert
#define furi_crash(...) abort()
typedef int FuriString;
void furi_string_cat_printf(FuriString* s,const char* f,...){(void)s;(void)f;}
'''
        body += source("lib/bit_lib/bit_lib.h") + "\n" + source("lib/bit_lib/bit_lib.c")
        body += "\n" + source("lib/toolbox/level_duration.h") + "\n" + text
        body += r'''
static const uint8_t expected[11]={0x12,0x34,0x56,0x78,0x91,0x23,0x80,1,0xAA,0x55,0xCC};
static void exercise(unsigned carrier,int skew,bool corrupt){
 ProtocolFDXB tx={0};memcpy(tx.data,expected,11);protocol_fdx_b_encoder_start(&tx);
 if(corrupt)bit_lib_set_bit(tx.encoded_data,84,!bit_lib_get_bit(tx.encoded_data,84));
 struct {uint64_t left;ProtocolFDXB value;uint64_t right;} rx;
 memset(&rx,0xA5,sizeof(rx));protocol_fdx_b_decoder_start(&rx.value);
 unsigned found=0;
 for(unsigned i=0;i<12000;i++){
  LevelDuration sample=protocol_fdx_b_encoder_yield(&tx);
  bool level=level_duration_get_level(sample);
  int32_t us=(int32_t)(((uint64_t)level_duration_get_duration(sample)*8*125000)/carrier);
  us+=level?skew:-skew;assert(us>0);
  if(protocol_fdx_b_decoder_feed(&rx.value,level,(uint32_t)us)){
   found++;assert(!memcmp(rx.value.data,expected,11));
  }
 }
 assert(corrupt ? found==0 : found>0);
 for(unsigned i=0;i<10000;i++){
  protocol_fdx_b_decoder_feed(&rx.value,true,UINT32_MAX-i);
  protocol_fdx_b_decoder_feed(&rx.value,false,100+i%350);
 }
 for(unsigned period=190;period<570;period++){
  protocol_fdx_b_decoder_start(&rx.value);
  uint32_t high=INT32_MAX-16u;
  protocol_fdx_b_decoder_feed(&rx.value,true,high);
  protocol_fdx_b_decoder_feed(&rx.value,false,UINT32_MAX-high+1u+period);
 }
 assert(rx.left==UINT64_C(0xA5A5A5A5A5A5A5A5));
 assert(rx.right==UINT64_C(0xA5A5A5A5A5A5A5A5));
}
int main(int argc,char** argv){assert(argc==2);unsigned mode=atoi(argv[1]);
 if(mode==0)exercise(125000,0,false);
 if(mode==1)exercise(125000,80,false);
 if(mode==2)exercise(134200,70,false);
 if(mode==3)exercise(134200,70,true);
 return 0;}
'''
        with tempfile.TemporaryDirectory(prefix="fdxb-duty-test-") as directory:
            work = Path(directory); (work / "test.c").write_text(body)
            compiled = subprocess.run(["cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                       str(work / "test.c"), "-o", str(work / "test")],
                                      capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for mode in range(4):
                with self.subTest(mode=mode):
                    ran = subprocess.run([str(work / "test"), str(mode)], capture_output=True, text=True)
                    self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
