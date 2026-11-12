"""Frozen custom-button behavior before flash compaction; no keys or RF."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_subghz_compact_buttons import PRELUDE

ROOT = Path(__file__).resolve().parents[2]
# Filled from the unchanged function in f9501820, before its refactor.
GOLDEN_SHA256 = "699cac150495cb9b0d92f441629cdb61212034002d681f3ca92948e4a71da4c8"


def button_output():
    source = (ROOT / "lib/subghz/protocols/keeloq.c").read_text()
    production = function(source, "static uint8_t subghz_protocol_keeloq_get_btn_code(uint8_t last_btn_code) {")
    with tempfile.TemporaryDirectory(prefix="keeloq-buttons-") as directory:
        root = Path(directory)
        (root / "test.c").write_text(PRELUDE + production + r'''
int main(void){
    for(unsigned o=0;o<256;o++)for(unsigned d=0;d<256;d++)for(unsigned last=0;last<256;last++){
        original=o;direction=d;putchar(subghz_protocol_keeloq_get_btn_code(last));
    }
    return 0;
}
''')
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(root / "test.c"), "-o", str(root / "test")],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(root / "test")], check=True, capture_output=True).stdout


class KeeLoqButtonEquivalenceTest(unittest.TestCase):
    def test_all_original_direction_and_fallback_values_match_pre_compaction(self):
        output = button_output()
        self.assertEqual(len(output), 256 ** 3)
        self.assertEqual(hashlib.sha256(output).hexdigest(), GOLDEN_SHA256)


if __name__ == "__main__":
    unittest.main()
