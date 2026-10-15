"""Unknown presets must never become an unsigned array index or stale radio setup."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class UnknownPresetTests(unittest.TestCase):
    def test_rx_lookup_failure_never_starts_a_radio(self):
        paths = (
            "applications/main/subghz/helpers/subghz_txrx.c",
            "applications_user/arf_subghz_full/helpers/subghz_txrx.c",
            "applications/main/subghz_remote/helpers/txrx/subghz_txrx.c",
            "applications_user/flipper_companion/helpers/subghz_txrx.c",
            "applications_user/quac/actions/helpers/subghz_txrx.c",
            "applications_user/subghz_wardriving/helpers/subghz_wardriving_txrx.c",
        )
        for path in paths:
            with self.subTest(path=path):
                prefix = "subghz_wardriving_txrx" if "wardriving" in path else "subghz_txrx"
                body = function((ROOT / path).read_text(), f"void {prefix}_rx_start(")
                native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define furi_assert(x) assert(x)
#define FURI_LOG_E(...) ((void)0)
typedef struct {const char* name;uint32_t frequency;} Preset;
typedef struct {Preset* preset;void* setting;} SubGhzTxRx;
static bool missing;
static unsigned begins,starts;
static uint8_t data;
static const char* furi_string_get_cstr(const char* s){return s;}
static void subghz_txrx_stop(SubGhzTxRx* s){(void)s;}
__attribute__((unused)) static void subghz_txrx_receiver_reset(SubGhzTxRx* s){(void)s;}
static void* subghz_txrx_get_setting(SubGhzTxRx* s){return s->setting;}
static uint8_t* subghz_setting_get_preset_data_by_name(void* s,const char* n){(void)s;(void)n;return missing?NULL:&data;}
static void subghz_txrx_begin(SubGhzTxRx* s,uint8_t* d){(void)s;assert(d);begins++;}
static void subghz_txrx_rx(SubGhzTxRx* s,uint32_t f){(void)s;(void)f;starts++;}
#define subghz_wardriving_txrx_stop subghz_txrx_stop
#define subghz_wardriving_txrx_get_setting subghz_txrx_get_setting
#define subghz_wardriving_txrx_begin subghz_txrx_begin
#define subghz_wardriving_txrx_rx subghz_txrx_rx
''' + body + f'''
int main(void){{
    Preset p={{.name="missing",.frequency=433920000}};SubGhzTxRx t={{.preset=&p}};
    missing=true;{prefix}_rx_start(&t);assert(!begins && !starts);
    missing=false;{prefix}_rx_start(&t);assert(begins==1 && starts==1);
    return 0;
}}
''')

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
