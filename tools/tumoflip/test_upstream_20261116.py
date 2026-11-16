"""Native regressions for the November16 upstream reliability boundary."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


def syntax(body):
    with tempfile.TemporaryDirectory(prefix="tumo-nov16-syntax-") as directory:
        path = Path(directory) / "check.c"
        path.write_text(body)
        return subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Werror", "-fsyntax-only", str(path)],
            capture_output=True, text=True,
        )


class UpstreamNovember16Tests(unittest.TestCase):
    def test_internal_radio_noop_begin_and_external_failures(self):
        source = (ROOT / "lib/subghz/devices/devices.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define furi_check assert
typedef struct {unsigned ver; bool extended_range,amp_and_leds;} SubGhzDeviceConf;
typedef struct {bool (*begin)(SubGhzDeviceConf*);} Interconnect;
typedef struct {const Interconnect* interconnect;} SubGhzDevice;
static bool furi_hal_subghz_get_ext_leds_and_amp(void){return true;}
static unsigned called;
static bool good(SubGhzDeviceConf* c){assert(c->ver==1 && !c->extended_range && c->amp_and_leds);called++;return true;}
static bool bad(SubGhzDeviceConf* c){(void)c;called++;return false;}
''' + function(source, "bool subghz_devices_begin(") + r'''
int main(void){
 Interconnect hooks={0};SubGhzDevice d={&hooks};
 assert(subghz_devices_begin(&d));assert(!called);
 hooks.begin=good;assert(subghz_devices_begin(&d));assert(called==1);
 hooks.begin=bad;assert(!subghz_devices_begin(&d));assert(called==2);
 return 0;
}
''')

    def test_pps_macro_preserves_expression_precedence(self):
        source = (ROOT / "lib/nfc/helpers/iso14443_4_layer.c").read_text()
        constants = "\n".join(re.findall(
            r"(?m)^#define ISO14443_4_BLOCK_PPS_START(?:_MASK)?\s+.*$", source))
        macro = re.search(
            r"#define ISO14443_4_BLOCK_PPS_IS_START\(pps\) \\\n[^\n]+", source).group()
        native("#include <assert.h>\n" + constants + "\n" + macro + r'''
int main(void){
 for(unsigned value=0;value<256;value++)
  assert(ISO14443_4_BLOCK_PPS_IS_START(value)==((value&0xF0)==0xD0));
 assert(ISO14443_4_BLOCK_PPS_IS_START(0xC1 | 0x10));
 assert(!ISO14443_4_BLOCK_PPS_IS_START(0xD0 ^ 0x10));
 return 0;
}
''')

    def test_type4_error_logs_compile_with_actual_integer_widths(self):
        source = (ROOT / "lib/nfc/protocols/type_4_tag/type_4_tag_poller_i.c").read_text()
        calls = []
        for name in ("type_4_tag_poller_iso_read", "type_4_tag_poller_iso_write"):
            body = function(source, "static Type4TagError " + name + "(")
            self.assertIn("uint16_t length", body)
            calls.extend(re.findall(r"FURI_LOG_E\s*\(.*?\);", body, re.S))
        result = syntax(r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define FURI_LOG_E(tag, ...) printf(__VA_ARGS__)
static size_t bit_buffer_get_size_bytes(void* b){(void)b;return 1;}
void check(void){uint16_t length=17;uint8_t chunk_len=3;
 struct {void* rx_buffer;} data={0};void* unused=&data;(void)unused;
 struct {void* rx_buffer;}* instance=(void*)&data;
''' + "\n".join(calls) + "\n}\n")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ultralight_layout_guard_rejects_an_inserted_public_field(self):
        header = (ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight.h").read_text()
        source = (ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight.c").read_text()
        layout = re.search(
            r"typedef struct \{\s*Iso14443_3aData\* iso14443_3a_data;.*?\} MfUltralightData;",
            header, re.S).group()
        guards = ""
        if "MfUltralightDataUpstreamLayout" in source:
            start = source.index("typedef struct {", source.index("mf_ultralight_data_format_version"))
            guards = source[start:source.index("static const MfUltralightFeatures", start)]
        prelude = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void Iso14443_3aData;
typedef int MfUltralightType;
typedef struct {uint8_t data[8];} MfUltralightVersion;
typedef struct {uint8_t data[32];} MfUltralightSignature;
typedef struct {uint8_t data[3];} MfUltralightCounter;
typedef struct {uint8_t data;} MfUltralightTearingFlag;
typedef struct {uint8_t data[4];} MfUltralightPage;
typedef struct {uint8_t data[16];} MfUltralightAesKey;
#define MF_ULTRALIGHT_COUNTER_NUM 3
#define MF_ULTRALIGHT_TEARING_FLAG_NUM 3
#define MF_ULTRALIGHT_MAX_PAGE_NUM 256
#define MF_ULTRALIGHT_AES_SIGNATURE_SIZE 48
'''
        good = syntax(prelude + layout + "\n" + guards)
        self.assertEqual(good.returncode, 0, good.stderr)
        mutation = layout.replace("    MfUltralightCounter counter", "    uint8_t abi_break;\n    MfUltralightCounter counter")
        self.assertNotEqual(mutation, layout)
        bad = syntax(prelude + mutation + "\n" + guards)
        self.assertNotEqual(bad.returncode, 0, "A public layout insertion compiled without an ABI guard")
        self.assertIn("moved", bad.stderr)


if __name__ == "__main__":
    unittest.main()
