"""Exercise the production discriminator branch without cryptographic keys."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class WisniowskiVariantTests(unittest.TestCase):
    def test_variants_keep_the_full_discriminator(self):
        source = (ROOT / "lib/subghz/protocols/keeloq.c").read_text()
        location = source.index('strcmp(instance->manufacture_name, "Wisniowski")')
        begin = source.rfind("else if(", 0, location) + len("else ")
        end = source.index("} else if(", location) + 1
        branch = source[begin:end]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct {const char* manufacture_name; struct {uint32_t serial,cnt;} generic;} Encoder;
static uint32_t fields(const char* name) {
    Encoder data={.manufacture_name=name,.generic={0xC34,0x1234}};
    Encoder* instance=&data; uint32_t btn=2;
    uint32_t decrypt=btn<<28 | (instance->generic.serial & 0x3FF)<<16 | instance->generic.cnt;
''' + branch + r'''
    return decrypt;
}
int main(void) {
    assert(fields("Wisniowski")==0x2C341234);
    assert(fields("Wisniowski2")==0x2C341234);
    assert(fields("Wisniowski1Rv")==0x2C341234);
    assert(fields("Pecinin")==0x2C341234);
    assert(fields("Wisniowski2-other")==0x20341234);
    assert(fields("Unknown")==0x20341234);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
