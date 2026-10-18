"""Offline Nice framing checks: synthetic inputs, no radio or receiver enrollment."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class NiceRawPassiveTests(unittest.TestCase):
    def test_all_72_bit_parcels_and_plain_nice_one(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
bool subghz_protocol_blocks_get_bit_array(uint8_t* p,unsigned bit){return(p[bit/8]>>(7-bit%8))&1;}
''' + function(source, "static uint32_t subghz_protocol_nice_one_crc(") + "\n" +
            function(source, "static void subghz_protocol_nice_one_get_data(uint8_t* p, uint8_t num_parcel, uint8_t hold_bit) {") + r'''
int main(void){
 const uint8_t expected[16]={0x8f,0x8e,0x8d,0x8c,0x84,0x85,0x86,0x87,0x8f,0x8c,0x89,0x8d,0x8c,0x8d,0x8e,0x8f};
 for(unsigned parcel=0;parcel<16;parcel++){
  for(unsigned release=0;release<2;release++){
   uint8_t p[10]={1,2,3,4,5,6,7};
   subghz_protocol_nice_one_get_data(p,parcel,4|release);
   assert(p[7]==expected[parcel]);
   assert((((p[8]>>4)^p[7])&3)==release);
   assert(((p[8]&15)<<4|(p[9]>>4))==subghz_protocol_nice_one_crc(p));
   uint8_t plain[10]={1,2,3,4,5,6,7};
   subghz_protocol_nice_one_get_data(plain,parcel,release);
   assert(plain[7]==(parcel^(parcel<4?0x8f:0x80)));
   assert((((plain[8]>>4)^plain[7])&3)==release);
  }
 }
 return 0;
}
''')

    def test_offline_decode_reuses_standard_reader_and_defers_launch(self):
        raw = (ROOT / "applications/main/subghz/scenes/subghz_scene_decode_raw.c").read_text()
        more = (ROOT / "applications/main/subghz/scenes/subghz_scene_more_raw.c").read_text()
        nice = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        self.assertIn("subghz_raw_decode_sample", raw)
        self.assertIn("subghz_raw_decode_sample", nice)
        self.assertIn("Nice O-Code", more)
        self.assertIn("NiceOSceneRaw", nice)
        self.assertIn("nice_o_raw_begin", nice)

    def test_passive_certificate_has_a_separate_checked_parser(self):
        header = ROOT / "applications/system/nice_o_code/nice_o_certificate.h"
        self.assertTrue(header.exists())
        text = header.read_text()
        self.assertIn("nice_o_certificate_decode", text)


if __name__ == "__main__":
    unittest.main()
