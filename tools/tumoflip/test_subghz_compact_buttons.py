"""Exhaustive button equivalence against the pre-compaction 523c937 snapshot."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = ROOT / "lib/subghz/blocks"
LABELS = ROOT / "applications/main/subghz/helpers/subghz_button_labels.c"
GOLDEN = Path(__file__).parent / "fixtures/subghz_buttons_523c937.json"
PROTOCOLS = (
    "princeton", "nice_flor_s", "came_atomo", "alutech_at_4n",
    "kinggates_stylo_4k", "ditec_gol4", "somfy_telis", "gangqi",
    "hollarm", "roger", "secplus_v2",
)
PRELUDE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define SUBGHZ_CUSTOM_BTN_OK 0
#define SUBGHZ_CUSTOM_BTN_UP 1
#define SUBGHZ_CUSTOM_BTN_DOWN 2
#define SUBGHZ_CUSTOM_BTN_LEFT 3
#define SUBGHZ_CUSTOM_BTN_RIGHT 4
static uint8_t original, direction;
uint8_t subghz_custom_btn_get(void) { return direction; }
uint8_t subghz_custom_btn_get_original(void) { return original; }
'''


def lookup_source():
    source = (BLOCKS / "custom_btn.c").read_text()
    signature = "uint8_t subghz_custom_btn_lookup("
    return function(source, signature) if signature in source else ""


def protocol_output(name):
    source = (ROOT / f"lib/subghz/protocols/{name}.c").read_text()
    signature = f"static uint8_t subghz_protocol_{name}_get_btn_code(void) {{"
    body = PRELUDE + lookup_source() + function(source, signature) + f'''
int main(void) {{
    for(unsigned o=0;o<256;o++) {{
        original=o;
        for(unsigned d=0;d<256;d++) {{
            direction=d;
            putchar(subghz_protocol_{name}_get_btn_code());
        }}
    }}
    return 0;
}}
'''
    with tempfile.TemporaryDirectory(prefix="subghz-buttons-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(body)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-O1",
                        "-fsanitize=address,undefined", str(path / "test.c"),
                        "-o", str(path / "test")], check=True, capture_output=True)
        return subprocess.run([str(path / "test")], check=True, capture_output=True).stdout


class CompactButtonTests(unittest.TestCase):
    def test_every_original_and_direction_matches_pre_compaction(self):
        golden = json.loads(GOLDEN.read_text())
        for name in PROTOCOLS:
            with self.subTest(protocol=name):
                output = protocol_output(name)
                self.assertEqual(len(output), 65536)
                self.assertEqual(hashlib.sha256(output).hexdigest(), golden[name])

    def test_shared_lookup_preserves_unknown_values(self):
        native(PRELUDE + "\nuint8_t subghz_custom_btn_lookup(const uint8_t (*)[5], size_t);\n"
               + lookup_source() + r'''
int main(void) {
    const uint8_t rows[][5]={{0,1,2,3,4},{255,254,253,252,251}};
    for(unsigned o=0;o<256;o++) {
        original=o;
        for(unsigned d=0;d<256;d++) {
            direction=d;
            uint8_t expected=o;
            if(d>=1 && d<=4) {
                if(o==0) expected=d;
                if(o==255) expected=255-d;
            }
            assert(subghz_custom_btn_lookup(rows,2)==expected);
            assert(subghz_custom_btn_lookup(NULL,0)==o);
        }
    }
    return 0;
}
''')

    def test_label_records_are_compact_and_behavior_unchanged(self):
        source = LABELS.read_text()
        source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
        fixture = (Path(__file__).parent / "fixtures/subghz_labels_523c937.c").read_text()
        native(PRELUDE + '\n#include <string.h>\n#define SUBGHZ_BUTTON_LABEL_COUNT 8\n'
               + source + fixture)


if __name__ == "__main__":
    unittest.main()
