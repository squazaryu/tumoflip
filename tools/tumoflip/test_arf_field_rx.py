"""ARF passive protocol adaptation: synthetic inputs, never radio or key search."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/field_rx_protocols"
BOUNDARY = "9b18419de2774a9693f91a3631653d1538f0f91a"


def source(name):
    path = APP / (name + ".c")
    return path.read_text() if path.exists() else subprocess.check_output(
        ["git", "show", BOUNDARY + ":lib/subghz/protocols/" + name + ".c"], cwd=ROOT, text=True)


class ArfFieldRxTests(unittest.TestCase):
    def test_all_adapted_leaves_are_receive_only_and_bound_malformed_pulses(self):
        for name in ("x10", "urmet", "gm_v0", "gm_v1", "toyota_v0", "mazda_v2", "mercedes_v0", "mercedes_v1", "opel_v1", "bmw"):
            with self.subTest(protocol=name):
                code = source(name)
                symbol = re.search(r"const SubGhzProtocol\s+(\w+)\s*=", code)[1]
                header = re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / (name + ".h")).read_text(), flags=re.M)
                code = re.sub(r"^#include.*\n", "\n", code, flags=re.M)
                program = (ROOT / "tools/tumoflip/fixtures/field_rx_native.h").read_text() + header + code + r'''
static unsigned hits;
static void found(SubGhzProtocolDecoderBase*b,void*c){(void)b;(void)c;hits++;}
int main(void){
 const SubGhzProtocol*p=&PROTOCOL;assert(p->decoder&&!p->encoder&&!(p->flag&SubGhzProtocolFlag_Send));
 void*d=p->decoder->alloc(NULL);assert(d);((SubGhzProtocolDecoderBase*)d)->callback=found;p->decoder->reset(d);
 const uint32_t durations[]={0,1,25,50,79,80,100,120,125,150,240,250,300,400,500,600,1000,1800,2000,5000,9600,12000,20000,0xffffffff};
 uint32_t random=12345;
 for(unsigned i=0;i<200000;i++){random=random*1664525u+1013904223u;p->decoder->feed(d,(random>>8)&1,durations[random%COUNT_OF(durations)]);if(i%479==0)p->decoder->reset(d);}
 FlipperFormat f={0};assert(p->decoder->deserialize(d,&f)!=SubGhzProtocolStatusOk);
 p->decoder->get_hash_data(d);FuriString text={0};p->decoder->get_string(d,&text);
 p->decoder->free(d);return 0;
}
'''.replace("PROTOCOL", symbol)
                with tempfile.TemporaryDirectory(prefix="field-rx-native-") as directory:
                    work = Path(directory)
                    (work / "test.c").write_text(program)
                    # F7 uses32-bit long; the host fixture deliberately ignores
                    # only printf format-width diagnostics, not memory/UB errors.
                    built = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                            "-Wno-format", "-Wno-unused-function", "-Wno-unused-variable",
                                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", str(ROOT),
                                            str(work / "test.c"), str(ROOT / "lib/toolbox/manchester_decoder.c"), "-o", str(work / "test")],
                                           capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stderr)
                    ran = subprocess.run([str(work / "test")], capture_output=True, text=True, timeout=10)
                    self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_x10_saved_files_validate_status_bitcount_and_complement_bytes(self):
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
typedef enum{SubGhzProtocolStatusOk,SubGhzProtocolStatusError,SubGhzProtocolStatusErrorParserOthers,SubGhzProtocolStatusErrorValueBitCount}SubGhzProtocolStatus;
typedef struct{uint64_t data;uint16_t data_count_bit;}Generic;
typedef struct{Generic generic;}SubGhzProtocolDecoderX10;
typedef struct{uint64_t data;uint16_t bits;bool fail;}FlipperFormat;
static const struct{unsigned min_count_bit_for_found;}subghz_protocol_x10_const={32};
static SubGhzProtocolStatus subghz_block_generic_deserialize(Generic*g,FlipperFormat*f){g->data=f->data;g->data_count_bit=f->bits;return f->fail?SubGhzProtocolStatusError:SubGhzProtocolStatusOk;}
''' + function(source("x10"), "SubGhzProtocolStatus\n    subghz_protocol_decoder_x10_deserialize(") + r'''
int main(void){SubGhzProtocolDecoderX10 d={0};FlipperFormat f={.data=0x12ed34cb,.bits=31};
 assert(subghz_protocol_decoder_x10_deserialize(&d,&f)!=SubGhzProtocolStatusOk);
 f.bits=32;assert(subghz_protocol_decoder_x10_deserialize(&d,&f)==SubGhzProtocolStatusOk);
 f.data^=1;assert(subghz_protocol_decoder_x10_deserialize(&d,&f)!=SubGhzProtocolStatusOk);
 f.data=0x12ed34cb;f.fail=true;assert(subghz_protocol_decoder_x10_deserialize(&d,&f)!=SubGhzProtocolStatusOk);
 return 0;}
''')


if __name__ == "__main__":
    unittest.main()
