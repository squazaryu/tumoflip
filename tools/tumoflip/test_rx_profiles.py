"""Reception profile selection must not depend on an unchecked model DB index."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]


class RxProfilesTest(unittest.TestCase):
    def test_profiles_do_not_shift_protopirate_plugin_context_layout(self):
        header = (ROOT / "applications_user/protopirate/protopirate_app_i.h").read_text()
        self.assertNotIn("VariableItem* rx_profile_item;", header)

    def test_preset_registration_does_not_publish_partial_allocations(self):
        source = (ROOT / "lib/subghz/subghz_setting.c").read_text()
        body = function(source, "bool subghz_setting_load_custom_preset(")
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
typedef int FlipperFormat;typedef int FuriString;
typedef struct {FuriString* custom_preset_name;uint32_t custom_preset_data_size;uint8_t* custom_preset_data;} SubGhzSettingCustomPresetItem;
typedef struct {struct {int data;}*preset;}SubGhzSetting;
static unsigned added;static int mode;static SubGhzSettingCustomPresetItem item;
static SubGhzSettingCustomPresetItem* SubGhzSettingCustomPresetItemArray_push_raw(int a){(void)a;added++;return &item;}
static FuriString* furi_string_alloc(void){static int s;return &s;}
static void furi_string_set(FuriString*s,const char*v){(void)s;(void)v;}
static bool flipper_format_get_value_count(FlipperFormat*f,const char*k,uint32_t*n){(void)f;(void)k;*n=mode==1?3:4;return mode!=0;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*d,size_t n){(void)f;(void)k;(void)d;(void)n;return mode==3;}
''' + body + r'''
int main(void){struct{int data;}preset={0};SubGhzSetting s={(void*)&preset};int ff=0;
 for(mode=0;mode<3;mode++){added=0;assert(!subghz_setting_load_custom_preset(&s,"test",&ff));assert(added==0);}
 mode=3;assert(subghz_setting_load_custom_preset(&s,"test",&ff));assert(added==1&&item.custom_preset_data_size==4);free(item.custom_preset_data);return 0;}
''')

    def test_catalog_is_bounded_and_data_is_owned_by_settings(self):
        path = ROOT / "lib/subghz/subghz_rx_profiles.h"
        self.assertTrue(path.exists(), "shared RX profile catalog missing")
        source = "\n".join(l for l in path.read_text().splitlines() if not l.startswith(("#pragma once", "#include")))
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define COUNT_OF(x) (sizeof(x)/sizeof((x)[0]))
typedef struct {bool added;} SubGhzSetting;typedef int FlipperFormat;
static bool write_ok=true,load_ok=true,collision=false;static int allocations;
static int subghz_setting_get_inx_preset_by_name(SubGhzSetting*s,const char*n){if(!strcmp(n,"AM650"))return 0;if(!strcmp(n,"FM476"))return 1;return s->added?2:-1;}
static uint8_t bytes[128];static size_t byte_count;
static uint8_t* subghz_setting_get_preset_data(SubGhzSetting*s,size_t i){(void)s;(void)i;return bytes;}
static size_t subghz_setting_get_preset_data_size(SubGhzSetting*s,size_t i){(void)s;(void)i;return collision?0:byte_count;}
static FlipperFormat* flipper_format_string_alloc(void){static int f;allocations++;return &f;}
static bool flipper_format_write_hex(FlipperFormat*f,const char*k,const uint8_t*d,size_t n){(void)f;(void)k;memcpy(bytes,d,n);byte_count=n;return write_ok;}
static bool flipper_format_rewind(FlipperFormat*f){(void)f;return true;}
static void flipper_format_free(FlipperFormat*f){(void)f;allocations--;}
static bool subghz_setting_load_custom_preset(SubGhzSetting*s,const char*n,FlipperFormat*f){(void)n;(void)f;s->added=load_ok;return load_ok;}
''' + source + r'''
int main(void){SubGhzSetting s={0};assert(SUBGHZ_RX_PROFILE_COUNT==8);
 assert(!subghz_rx_profile_get(0));assert(!subghz_rx_profile_get(8));assert(!subghz_rx_profile_get(255));
 assert(subghz_rx_profiles_init(&s));assert(s.added&&!allocations);
 for(unsigned i=1;i<SUBGHZ_RX_PROFILE_COUNT;i++){const SubGhzRxProfile*p=subghz_rx_profile_get(i);assert(p&&p->frequency>=300000000&&p->frequency<=500000000);assert(subghz_rx_profile_preset_index(&s,i)>=0);}
 assert(subghz_rx_profiles_init(&s));assert(!allocations);
 collision=true;assert(!subghz_rx_profiles_init(&s));collision=false;
 s.added=false;write_ok=false;assert(!subghz_rx_profiles_init(&s));assert(!s.added&&!allocations);
 write_ok=true;load_ok=false;assert(!subghz_rx_profiles_init(&s));assert(!s.added&&!allocations);
 return 0;}
''')

    def test_standard_raw_and_protopirate_keep_separate_settings(self):
        core = (ROOT / "applications/main/subghz/scenes/subghz_scene_receiver_config.c").read_text()
        proto = (ROOT / "applications_user/protopirate/scenes/protopirate_scene_receiver_config.c").read_text()
        for source in (core, proto):
            self.assertIn('"RX profile"', source)
            self.assertIn("subghz_rx_profile_get", source)
            self.assertIn("subghz_rx_profile_preset_index", source)
            self.assertNotIn("car_model_index", source)
        self.assertIn("raw_frequency = profile->frequency", core)
        self.assertIn("raw_preset_index =", core)
        self.assertIn("frequency = profile->frequency", core)
        self.assertIn("hopping_mode != SubGhzHoppingModeOff", core)


if __name__ == "__main__":
    unittest.main()
