"""A failed profile load must not replace the active editor state."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class WeatherLoadTransactionTests(unittest.TestCase):
    def test_profile_commit_only_after_success(self):
        source = (ROOT / "applications_user/weather_editor/weather_editor_engine.c").read_text()
        start = source.index("bool weather_editor_load_profile(")
        end = source.index("\nconst char* weather_editor_battery_label", start)
        # The public wrapper must isolate the parser. The parser's field and
        # preset validation is exercised separately; here it fails after mutation.
        self.assertIn("weather_editor_read_profile", source[start:end])
        run_c(r'''
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct {int value;} FuriString;
typedef struct {FuriString* protocol_name; int payload;} WeatherEditorState;
typedef struct {FuriString* name; int frequency; uint8_t* data; unsigned data_size;} SubGhzRadioPreset;
static bool succeed;
static FuriString* furi_string_alloc(void) {return calloc(1,sizeof(FuriString));}
static void furi_string_free(FuriString* s) {free(s);}
static void furi_string_reset(FuriString* s) {(void)s;}
static void furi_string_set(FuriString* d,const FuriString* s) {*d=*s;}
static void furi_string_set_str(FuriString* d,const char* s) {(void)d;(void)s;}
static bool weather_editor_state_copy(WeatherEditorState* d,const WeatherEditorState* s) {
    *d->protocol_name=*s->protocol_name;d->payload=s->payload;return true;
}
static bool weather_editor_read_profile(const char* p,SubGhzRadioPreset* r,
    WeatherEditorState* s,uint8_t** data,FuriString* status) {
    (void)p;(void)status;s->payload=99;s->protocol_name->value=99;
    r->frequency=99;r->name->value=99;
    if(succeed) {*data=malloc(1);r->data=*data;r->data_size=1;}
    return succeed;
}
''' + source[start:end].replace('furi_string_set(status, "', 'furi_string_set_str(status, "') + r'''
int main(void) {
    for(int n=0;n<2;n++) {
        succeed=n;
        FuriString protocol={1},name={2};
        uint8_t old=3;uint8_t* data=&old;
        WeatherEditorState state={&protocol,4};
        SubGhzRadioPreset preset={&name,5,&old,1};
        assert(weather_editor_load_profile("profile",&preset,&state,&data,NULL)==succeed);
        assert(state.protocol_name==&protocol && preset.name==&name);
        if(succeed) {
            assert(state.payload==99 && preset.frequency==99 && data!=&old);
            assert(protocol.value==99 && name.value==99);free(data);
        } else {
            assert(state.payload==4 && preset.frequency==5 && data==&old);
            assert(protocol.value==1 && name.value==2 && preset.data==&old);
        }
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
