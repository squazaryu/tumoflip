"""Execute adapted production functions, including failure and lifetime boundaries."""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native as run_c

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text()


PRELUDE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define furi_assert assert
#define furi_check assert
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define CLAMP(x,hi,lo) ((x)>(hi)?(hi):((x)<(lo)?(lo):(x)))
#define UNUSED(x) (void)(x)
static int model_lock;
#define with_view_model(view,decl,body,update) do { decl=(view); model_lock++; body; model_lock--; } while(0)
"""


class Upstream1435NativeTests(unittest.TestCase):
    def test_stealth_moves_are_nonzero_signed_hid_deltas(self):
        text = source("applications/system/hid_app/views/hid_mouse_jiggler_stealth.c")
        run_c(PRELUDE + r'''
static int next_random;
#define rand() next_random
''' + function(text, "static int8_t hid_mouse_jiggler_stealth_random_move(") + r'''
int main(void) {
    (void)model_lock;bool found[255]={0};
    for(next_random=0;next_random<254;next_random++) {
        int delta=hid_mouse_jiggler_stealth_random_move();
        assert(delta && delta>=-127 && delta<=127);found[delta+127]=true;
    }
    for(int i=0;i<255;i++)assert(found[i]==(i!=127));
    return 0;
}
''')

    def test_jigglers_stop_exit_and_connection_gating(self):
        for stealth in (False, True):
            name = "hid_mouse_jiggler" + ("_stealth" if stealth else "")
            text = source(f"applications/system/hid_app/views/{name}.c")
            types = text[text.index("struct HidMouseJiggler"):text.index(f"static void {name}_draw_callback")]
            # The actual header supplies the transport policy, not a test-only substitute.
            header = source("applications/system/hid_app/hid.h")
            policy = re.search(r"#ifdef HID_TRANSPORT_BLE\n#define hid_model_connected.*?#endif", header, re.S)
            policy = policy.group(0) if policy else ""
            for ble in (False, True):
                with self.subTest(stealth=stealth, ble=ble):
                    typename = "HidMouseJiggler" + ("Stealth" if stealth else "")
                    fixture = PRELUDE + ("\n#define HID_TRANSPORT_BLE\n" if ble else "") + r"""
typedef void View;
typedef int Hid;
typedef struct {bool active;uint32_t period;} FuriTimer;
typedef struct HidMouseJiggler HidMouseJiggler;
typedef struct HidMouseJigglerStealth HidMouseJigglerStealth;
typedef struct {int type;int key;} InputEvent;
enum {InputTypePress,InputKeyOk,InputKeyLeft,InputKeyRight,InputKeyUp,InputKeyDown};
#define MOUSE_MOVE_SHORT 5
#define INTERVAL_MIN_MINUTES 1
#define INTERVAL_MAX_MINUTES 30
static int reports,flushes;
static uint32_t furi_ms_to_ticks(uint32_t ms) {return ms;}
static void furi_timer_start(FuriTimer* t,uint32_t p) {t->active=true;t->period=p;}
static void furi_timer_stop(FuriTimer* t) {assert(!model_lock);t->active=false;}
static void furi_timer_flush(void) {assert(!model_lock);flushes++;}
static void furi_timer_free(FuriTimer* t) {assert(!t->active);}
static void view_free(void* v) {(void)v;}
static void mock_free(void* p) {(void)p;}
#define free mock_free
static void hid_hal_mouse_move(Hid* h,int8_t x,int8_t y) {
    (void)h;(void)x;(void)y;assert(!model_lock);reports++;
}
""" + policy + "\n" + types
                    for suffix in ("timer_callback", "exit_callback", "input_callback"):
                        fixture += function(text, f"static {'bool' if suffix == 'input_callback' else 'void'} {name}_{suffix}(") + "\n"
                    fixture += function(text, f"void {name}_free(") + "\n"
                    fixture += f"\nint main(void) {{\n{typename}Model m={{0}};\nFuriTimer timer={{0}};\n{typename} app={{.view=&m,.timer=&timer}};\n"
                    fixture += "m.min_interval=1;m.max_interval=2;\n" if stealth else "m.interval_idx=0;\n"
                    fixture += f"""
    InputEvent ok={{InputTypePress,InputKeyOk}};
    assert({name}_input_callback(&ok,&app));
    assert(m.running && timer.active);
    {name}_timer_callback(&app);
    assert(reports=={0 if ble else 1});
    m.connected=true;
    {name}_timer_callback(&app);
    assert(reports=={1 if ble else 2});
    assert({name}_input_callback(&ok,&app));
    assert(!m.running && !timer.active && flushes==1);
    int before=reports;
    {name}_timer_callback(&app);assert(reports==before);
    {name}_input_callback(&ok,&app);
    {name}_exit_callback(&app);
    assert(!m.running && !timer.active && flushes==2);
    {name}_timer_callback(&app);assert(reports==before);
    {name}_input_callback(&ok,&app);assert(m.running && timer.active);
    {name}_exit_callback(&app);
    return 0;
}}
"""
                    # Helpers are intentionally used by the post-adaptation implementation.
                    arrows = f'''
    InputEvent other={{99,InputKeyOk}};
    assert(!{name}_input_callback(&other,&app));
    int keys_to_try[]={{InputKeyLeft,InputKeyRight,InputKeyUp,InputKeyDown}};
    for(size_t k=0;k<COUNT_OF(keys_to_try);k++)for(int n=0;n<65;n++){{
        InputEvent event={{InputTypePress,keys_to_try[k]}};
        {name}_input_callback(&event,&app);
        assert({'m.min_interval>=1 && m.max_interval<=30 && m.min_interval<=m.max_interval' if stealth else 'm.interval_idx>=0 && m.interval_idx<6'});
    }}
    {name}_input_callback(&ok,&app);
    int first={'m.min_interval' if stealth else 'm.interval_idx'};
    for(size_t k=0;k<COUNT_OF(keys_to_try);k++){{
        InputEvent event={{InputTypePress,keys_to_try[k]}};{name}_input_callback(&event,&app);
    }}
    assert(first=={'m.min_interval' if stealth else 'm.interval_idx'});
    {name}_exit_callback(&app);
    {name}_input_callback(&ok,&app);
    {name}_free(&app);assert(!m.running && !timer.active);
'''
                    fixture = fixture.replace("    return 0;\n}", arrows + "    return 0;\n}")
                    run_c(fixture.replace("static uint32_t furi_ms_to_ticks", "__attribute__((unused)) static uint32_t furi_ms_to_ticks").replace("static void furi_timer_flush", "__attribute__((unused)) static void furi_timer_flush"))

    def test_secplus_generation_frees_scratch_and_preserves_live_tx(self):
        text = source("applications/main/subghz/helpers/subghz_txrx_create_protocol_key.c")
        run_c(PRELUDE + r"""
typedef int SubGhzTransmitter;
typedef struct {SubGhzTransmitter* transmitter;void* environment;void* fff_data;void* preset;} SubGhzTxRx;
#define SUBGHZ_PROTOCOL_SECPLUS_V2_NAME "SecPlus"
static SubGhzTransmitter scratch,live;
static int allocations,frees;
static bool fail;
static SubGhzTransmitter* subghz_transmitter_alloc_init(void* e,const char* n) {
    (void)e;(void)n;if(fail)return NULL;allocations++;return &scratch;
}
static void subghz_txrx_set_preset(SubGhzTxRx* t,const char* n,uint32_t f,void* p,int s) {
    (void)t;(void)n;(void)f;(void)p;(void)s;
}
static SubGhzTransmitter* subghz_transmitter_get_protocol_instance(SubGhzTransmitter* t) {return t;}
static void subghz_protocol_secplus_v2_create_data(SubGhzTransmitter* t,void* d,uint32_t s,uint8_t b,uint32_t c,void* p) {
    (void)t;(void)d;(void)s;(void)b;(void)c;(void)p;
}
__attribute__((unused)) static void subghz_transmitter_free(SubGhzTransmitter* t) {if(t){assert(t==&scratch);frees++;}}
""" + function(text, "bool subghz_txrx_gen_secplus_v2_protocol(") + r"""
int main(void) {
    (void)model_lock;SubGhzTxRx app={.transmitter=&live};
    for(int i=0;i<100;i++) {
        assert(subghz_txrx_gen_secplus_v2_protocol(&app,"AM650",433920000,1,1,1));
        assert(allocations==frees);assert(app.transmitter==&live);
    }
    fail=true;assert(!subghz_txrx_gen_secplus_v2_protocol(&app,"AM650",433920000,1,1,1));
    assert(allocations==frees && app.transmitter==&live);
    return 0;
}
""")

    def test_all_generation_helpers_use_function_local_transmitters(self):
        text = source("applications/main/subghz/helpers/subghz_txrx_create_protocol_key.c")
        self.assertNotIn("->transmitter", text)
        self.assertEqual(text.count("subghz_transmitter_alloc_init("), 15)
        self.assertEqual(text.count("subghz_transmitter_free(transmitter)"), 15)
        txrx = source("applications/main/subghz/helpers/subghz_txrx.c")
        stop = function(txrx, "static void subghz_txrx_tx_stop(")
        self.assertIn("instance->transmitter = NULL", stop)

    def test_number_input_starts_inside_range(self):
        text = source("applications/services/gui/modules/number_input.c")
        run_c(PRELUDE + r"""
typedef char FuriString;
typedef void (*NumberInputCallback)(void*,int32_t);
typedef struct {NumberInputCallback callback;void* callback_context;int32_t current_number,min_value,max_value;char* text_buffer;} NumberInputModel;
typedef struct {void* view;} NumberInput;
static void furi_string_printf(char* s,const char* f,int32_t n) {(void)f;snprintf(s,64,"%d",n);}
static void furi_string_set(char* s,const char* value) {strcpy(s,value);}
""" + function(text, "void number_input_set_result_callback(") + r"""
int main(void) {
    char buffer[64];NumberInputModel model={.text_buffer=buffer};NumberInput input={.view=&model};
    int32_t ranges[][3]={{150000,159999,150000},{1,26,1},{-100,-1,-1},{0,100,0},{INT32_MIN,INT32_MAX,0}};
    for(size_t i=0;i<COUNT_OF(ranges);i++) {
        number_input_set_result_callback(&input,NULL,NULL,0,ranges[i][0],ranges[i][1]);
        assert(model.current_number==ranges[i][2]);
        assert(ranges[i][2] ? strtol(buffer,NULL,10)==ranges[i][2] && buffer[0] : !buffer[0]);
    }
    return 0;
}
""")

    def test_ibutton_exposes_configurable_write_targets(self):
        # Missing production feature, not a missing test dependency.
        path = ROOT / "lib/ibutton/ibutton_write_targets.c"
        self.assertTrue(path.is_file(), "iButton cannot restrict blank write attempts yet")
        text = path.read_text()
        header = source("lib/ibutton/ibutton_write_targets.h")
        self.assertIn("IBUTTON_WRITE_TARGET_MASK_ALL", header)
        self.assertIn("ibutton_write_target_write", text)

    def test_number_input_confirm_agrees_with_range_and_empty_state(self):
        text = source("applications/services/gui/modules/number_input.c")
        run_c(PRELUDE + r'''
#include <errno.h>
#define StrintParseNoError 0
typedef struct {const char* text_buffer;int32_t min_value,max_value,current_number;size_t selected_row,selected_column;void (*callback)(void*,int32_t);void* callback_context;} NumberInputModel;
typedef struct {char text;} NumberInputKey;
static const char enter_symbol='\r',backspace_symbol='\b',sign_symbol='-';
static const NumberInputKey keys[]={{'\r'}};
static const NumberInputKey* number_input_get_row(size_t row){(void)row;return keys;}
static void number_input_backspace_cb(NumberInputModel* m){(void)m;assert(false);}
static void number_input_sign(NumberInputModel* m){(void)m;assert(false);}
static void number_input_add_digit(NumberInputModel* m,char* c){(void)m;(void)c;assert(false);}
static bool furi_string_empty(const char* s){return !s[0];}
static const char* furi_string_get_cstr(const char* s){return s;}
static int strint_to_int64(const char* s,void* unused,int64_t* out,int base){
    (void)unused;char* end;errno=0;*out=strtoll(s,&end,base);return errno||*end||end==s;
}
static int saves;
static int32_t saved;
static void save(void* c,int32_t n){(void)c;saves++;saved=n;}
''' + function(text, "static bool number_input_get_value(")
        + function(text, "static bool is_number_too_large(")
        + function(text, "static bool is_number_too_small(")
        + function(text, "static void number_input_handle_ok(") + r'''
int main(void) {
    (void)model_lock;NumberInputModel m={.callback=save};
    const char* values[]={"", "-", "0", "1", "26", "27", "-1", "-2147483648", "2147483647", "999999999999999999999999"};
    int32_t ranges[][2]={{0,100},{1,26},{-100,-1},{INT32_MIN,INT32_MAX}};
    for(size_t r=0;r<COUNT_OF(ranges);r++)for(size_t i=0;i<COUNT_OF(values);i++){
        m.min_value=ranges[r][0];m.max_value=ranges[r][1];m.text_buffer=values[i];
        int64_t value=0;bool valid=number_input_get_value(&m,&value)&&value>=m.min_value&&value<=m.max_value;
        assert((!is_number_too_small(&m)&&!is_number_too_large(&m))==valid);
        saves=0;number_input_handle_ok(&m);assert(saves==(int)valid);
        if(valid)assert(saved==value);
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
