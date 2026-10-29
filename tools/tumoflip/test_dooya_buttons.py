"""Own-device Dooya commands: exercise actual upload generation, no RF I/O."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class DooyaButtonsTests(unittest.TestCase):
    def test_command_mapping_preserves_serial_channel_and_captured_ok(self):
        source = (ROOT / "lib/subghz/protocols/dooya.c").read_text()
        helpers = ""
        signature = "static uint8_t subghz_protocol_dooya_get_btn_code("
        if signature in source:
            helpers = function(source, signature)
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_assert assert
#define bit_read(v,b) (((v)>>(b))&1U)
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
#define SUBGHZ_CUSTOM_BTN_OK 0
#define SUBGHZ_CUSTOM_BTN_UP 1
#define SUBGHZ_CUSTOM_BTN_DOWN 2
#define SUBGHZ_CUSTOM_BTN_LEFT 3
#define SUBGHZ_CUSTOM_BTN_RIGHT 4
#define DOOYA_BTN_UP_LONG 0x11
#define DOOYA_BTN_UP_SHORT 0x1e
#define DOOYA_BTN_DOWN_LONG 0x33
#define DOOYA_BTN_DOWN_SHORT 0x3c
#define DOOYA_BTN_STOP 0x55
typedef struct {bool level;uint32_t duration;} LevelDuration;
static LevelDuration level_duration_make(bool l,uint32_t d){return (LevelDuration){l,d};}
typedef struct {uint64_t data;uint8_t btn;size_t data_count_bit;} SubGhzBlockGeneric;
typedef struct {SubGhzBlockGeneric generic;struct{LevelDuration*upload;size_t size_upload;}encoder;}SubGhzProtocolEncoderDooya;
static const struct {uint32_t te_short,te_long;} subghz_protocol_dooya_const={366,733};
static uint8_t command,original;static bool override;static uint8_t override_btn;
static uint8_t __attribute__((unused)) subghz_custom_btn_get(void){return command;}
static uint8_t __attribute__((unused)) subghz_custom_btn_get_original(void){return original;}
static void __attribute__((unused)) subghz_custom_btn_set_original(uint8_t b){original=b;}
static void __attribute__((unused)) subghz_custom_btn_set_max(uint8_t n){assert(n==4);}
static bool __attribute__((unused)) subghz_block_generic_global_button_override_get(uint8_t*b){if(override)*b=override_btn;return override;}
''' + helpers + "\n" + function(source, "static bool subghz_protocol_encoder_dooya_get_upload(") + r'''
int main(void){LevelDuration upload[128];const uint8_t captured[]={0x11,0x1e,0x33,0x3c,0x55,0x79,0xcc};
 const uint8_t expected[]={0,0x11,0x33,0x1e,0x3c};
 for(size_t b=0;b<sizeof(captured);b++){original=captured[b];for(command=0;command<5;command++){
  SubGhzProtocolEncoderDooya x={.generic={.data=0xe1dc030500ULL|original,.data_count_bit=40},.encoder={upload,128}};
  assert(subghz_protocol_encoder_dooya_get_upload(&x));
  assert(x.generic.data==(0xe1dc030500ULL|(command?expected[command]:original)));
  assert(x.encoder.size_upload==82);
  uint64_t decoded=0;for(unsigned i=0;i<40;i++){size_t p=3+i*2;assert(upload[p].level&&!upload[p+1].level);decoded=(decoded<<1)|(upload[p].duration==733);}
  assert(decoded==x.generic.data);
 }}
 command=1;original=0x55;override=true;override_btn=0x79;
 SubGhzProtocolEncoderDooya x={.generic={.data=0xe1dc030555ULL,.data_count_bit=40},.encoder={upload,128}};
 assert(subghz_protocol_encoder_dooya_get_upload(&x)&&x.generic.data==0xe1dc030579ULL);
 x.encoder.size_upload=1;assert(!subghz_protocol_encoder_dooya_get_upload(&x));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
