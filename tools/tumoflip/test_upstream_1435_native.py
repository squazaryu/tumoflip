"""Execute adapted production functions, including failure and lifetime boundaries."""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function, run_c

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
static void hid_hal_mouse_move(Hid* h,int8_t x,int8_t y) {
    (void)h;(void)x;(void)y;assert(!model_lock);reports++;
}
""" + policy + "\n" + types
                    for suffix in ("timer_callback", "exit_callback", "input_callback"):
                        fixture += function(text, f"static {'bool' if suffix == 'input_callback' else 'void'} {name}_{suffix}(") + "\n"
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
                    fixture += "\n"  # Silence only unused fixture functions on the RED baseline.
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

    def test_number_input_starts_inside_range(self):
        text = source("applications/services/gui/modules/number_input.c")
        run_c(PRELUDE + r"""
typedef char FuriString;
typedef void (*NumberInputCallback)(void*,int32_t);
typedef struct {NumberInputCallback callback;void* callback_context;int32_t current_number,min_value,max_value;char* text_buffer;} NumberInputModel;
typedef struct {void* view;} NumberInput;
static void furi_string_printf(char* s,const char* f,int32_t n) {(void)f;sprintf(s,"%d",n);}
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


if __name__ == "__main__":
    unittest.main()
