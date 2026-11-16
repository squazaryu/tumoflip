"""Received DEA discriminator retention; no cryptography, keys or RF calls."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "lib/subghz/protocols/keeloq.c"
PRELUDE = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define furi_assert assert
typedef struct {uint32_t serial,seed,cnt;}SubGhzBlockGeneric;
'''


class DeaMioMetadataTests(unittest.TestCase):
    def test_received_discriminator_survives_the_existing_normal_learning_check(self):
        source = PATH.read_text()
        new = "subghz_protocol_keeloq_check_decrypt_dea_mio"
        name = new if new in source else "subghz_protocol_keeloq_check_decrypt"
        defines = "\n".join(re.findall(r"(?m)^#define KEELOQ_DEA_MIO_.*$", source))
        actual = function(source, "static inline bool " + name + "(")
        native(PRELUDE + defines + "\n" + actual + f"\n#define check {name}\n" + r'''
int main(void){
 for(unsigned flags=0;flags<4;flags++)for(unsigned serial=1;serial<1024;serial++){
  const unsigned disc=(flags<<10)|serial;SubGhzBlockGeneric g={.seed=0x9999,.cnt=9};
  assert(check(&g,(2U<<28)|(disc<<16)|0x1234,2,serial&255));
  assert(g.seed==disc&&g.cnt==0x1234);
 }
 SubGhzBlockGeneric g={.seed=77,.cnt=88};
 assert(!check(&g,0x41231234,2,0x23));assert(g.seed==77&&g.cnt==88);
 assert(!check(&g,0x21231234,2,0x24));assert(g.seed==77&&g.cnt==88);
 assert(check(&g,0x20001234,2,0x55));assert(g.seed==0&&g.cnt==0x1234);
 return 0;
}
''')

    def test_existing_encoder_retains_nonzero_captured_flags_and_legacy_fallback(self):
        source = PATH.read_text()
        generator = function(source, "static bool subghz_protocol_keeloq_gen_data(")
        branch = re.search(
            r'else if\(strcmp\(instance->manufacture_name, "Dea_Mio"\) == 0\) \{(.*?)\n            \}',
            generator, re.S).group(1)
        actual = "\n".join(re.findall(r"(?m)^#define KEELOQ_DEA_MIO_.*$", source))
        if "static inline uint16_t subghz_protocol_keeloq_dea_mio_disc(" in source:
            actual += "\n" + function(source, "static inline uint16_t subghz_protocol_keeloq_dea_mio_disc(")
        native(PRELUDE + actual + r'''
typedef struct{SubGhzBlockGeneric generic;}Codec;
static unsigned encode_disc(uint32_t serial,uint32_t seed){
 Codec codec={.generic={.serial=serial,.seed=seed,.cnt=0x1234}};Codec*instance=&codec;
 const unsigned btn=2;uint32_t decrypt=0;
''' + branch + r'''
 return (decrypt>>16)&0xFFF;
}
int main(void){
 for(unsigned serial=1;serial<1024;serial++)for(unsigned flags=0;flags<4;flags++){
  unsigned seed=(flags<<10)|serial;assert(encode_disc(serial,seed)==seed);
  assert(encode_disc(serial,0)==((serial&0x3FF)|0xC00));
 }
 assert(encode_disc(0x123,0x124)==0xD23);
 assert(encode_disc(0,0)==0xC00); // zero remains the legacy unknown sentinel
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
