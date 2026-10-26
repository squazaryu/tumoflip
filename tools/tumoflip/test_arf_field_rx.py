"""ARF passive protocol adaptation: synthetic inputs, never radio or key search."""
from pathlib import Path
import subprocess
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
