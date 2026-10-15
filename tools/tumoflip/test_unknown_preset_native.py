"""Unknown presets must never become an unsigned array index or stale radio setup."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class UnknownPresetTests(unittest.TestCase):
    def test_missing_preset_returns_null_without_indexing(self):
        source = (ROOT / "lib/subghz/subghz_setting.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define furi_check(x) assert(x)
#define FURI_LOG_E(...) ((void)0)
typedef struct {char* custom_preset_name;uint8_t* custom_preset_data;} SubGhzSettingCustomPresetItem;
typedef struct {SubGhzSettingCustomPresetItem data[2];} Presets;
typedef struct {Presets* preset;} SubGhzSetting;
#define M_EACH(item,array,type) (SubGhzSettingCustomPresetItem* item=(array);item<(array)+2;item++)
static const char* furi_string_get_cstr(const char* s){return s;}
static unsigned accesses;
static SubGhzSettingCustomPresetItem* SubGhzSettingCustomPresetItemArray_get(
    SubGhzSettingCustomPresetItem* a,size_t i){assert(i<2);accesses++;return &a[i];}
''' + function(source, "int subghz_setting_get_inx_preset_by_name(") +
               function(source, "uint8_t* subghz_setting_get_preset_data_by_name(") + r'''
int main(void){
    uint8_t a=1,b=2;Presets presets={.data={{"AM650",&a},{"CUSTOM",&b}}};
    SubGhzSetting setting={.preset=&presets};
    assert(subghz_setting_get_preset_data_by_name(&setting,"AM650")==&a);
    assert(subghz_setting_get_preset_data_by_name(&setting,"CUSTOM")==&b);
    unsigned before=accesses;
    assert(subghz_setting_get_preset_data_by_name(&setting,"missing")==NULL);
    assert(subghz_setting_get_preset_data_by_name(&setting,"")==NULL);
    assert(subghz_setting_get_preset_data_by_name(&setting,NULL)==NULL);
    assert(accesses==before);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
