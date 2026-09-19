"""Synthetic received-field validation; no keys, encryption or RF generation."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class KeeLoqReceiveMetadataTest(unittest.TestCase):
    def test_received_fields_and_existing_transmit_boundary(self):
        source = (ROOT / "lib/subghz/protocols/keeloq.c").read_text()
        production = "\n".join(re.findall(r"^#define KEELOQ_(?:MONARCH|KEY)_.*$", source, re.M))
        for signature in ("static inline bool subghz_protocol_keeloq_check_decrypt_monarch(",
                          "static inline bool subghz_protocol_keeloq_check_decrypt_key(",
                          "static bool subghz_protocol_keeloq_receive_only("):
            if signature in source:
                production += "\n" + function(source, signature)
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define furi_assert assert
typedef struct {uint32_t cnt,seed;} SubGhzBlockGeneric;
bool subghz_protocol_keeloq_check_decrypt_monarch(SubGhzBlockGeneric*,uint32_t,uint8_t);
bool subghz_protocol_keeloq_check_decrypt_key(SubGhzBlockGeneric*,uint32_t,uint8_t);
bool subghz_protocol_keeloq_receive_only(const char*,uint32_t);
''' .replace("bool subghz_protocol_keeloq_check_decrypt_", "static inline bool subghz_protocol_keeloq_check_decrypt_")
            .replace("bool subghz_protocol_keeloq_receive_only", "static bool subghz_protocol_keeloq_receive_only")
            + production + r'''
int main(void){
 SubGhzBlockGeneric g={99,77};
 assert(!subghz_protocol_keeloq_check_decrypt_monarch(&g,0x41001234,2));assert(g.cnt==99 && g.seed==77);
 assert(!subghz_protocol_keeloq_check_decrypt_monarch(&g,0x2FFF1234,2));
 assert(subghz_protocol_keeloq_check_decrypt_monarch(&g,0x21001234,2));assert(g.seed==0x100 && g.cnt==0x1234);
 assert(subghz_protocol_keeloq_check_decrypt_monarch(&g,0x22805678,2));assert(g.seed==0x280 && g.cnt==0x5678);
 assert(!subghz_protocol_keeloq_check_decrypt_key(&g,0x22FF3456,4));assert(g.cnt==0x5678);
 assert(!subghz_protocol_keeloq_check_decrypt_key(&g,0x21003456,2));
 assert(subghz_protocol_keeloq_check_decrypt_key(&g,0x22FF3456,2));assert(g.cnt==0x3456);
 assert(subghz_protocol_keeloq_receive_only("KEY",0));
 assert(subghz_protocol_keeloq_receive_only("Monarch",0x280));
 assert(!subghz_protocol_keeloq_receive_only("Monarch",0));
 assert(!subghz_protocol_keeloq_receive_only("Monarch",0x100));
 assert(!subghz_protocol_keeloq_receive_only("BFT",0x280));
 return 0;
}
''')

    def test_capture_metadata_and_encoder_do_not_enable_new_transmission(self):
        source = (ROOT / "lib/subghz/protocols/keeloq.c").read_text()
        serialize = function(source, "SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(")
        self.assertIn('strcmp(instance->manufacture_name, "Monarch")', serialize)
        self.assertIn('"Seed"', serialize)
        encoder = function(source, "    subghz_protocol_encoder_keeloq_deserialize(")
        self.assertLess(encoder.index("subghz_protocol_keeloq_receive_only"),
                        encoder.index("subghz_protocol_encoder_keeloq_get_upload"))
        generator = function(source, "static bool subghz_protocol_keeloq_gen_data(")
        self.assertNotIn('strcmp(instance->manufacture_name, "KEY")', generator)
        self.assertNotIn("monarch_disc", generator)


if __name__ == "__main__":
    unittest.main()
