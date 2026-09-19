"""Bounded duration parsing and cancellation of the production wait adapter."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
QUAC = ROOT / "applications_user/quac"


class QuacDurationTest(unittest.TestCase):
    def test_settings_and_playlists_validate_before_using_values(self):
        settings = (QUAC / "quac_settings.c").read_text()
        self.assertGreaterEqual(settings.count("quac_duration_valid(temp_data32)"), 4)
        playlist = (QUAC / "actions/action_qpl.c").read_text()
        self.assertGreaterEqual(playlist.count("quac_duration_parse("), 5)
        self.assertNotIn("furi_delay_ms(pause_length)", playlist)

    def test_raw_wait_has_cancellation_and_keeps_callback_alive_until_stop(self):
        source = (QUAC / "actions/action_subghz.c").read_text()
        self.assertIn("quac_action_wait_complete(context)", source)
        self.assertIn("quac_action_wait_run(wait, FuriWaitForever)", source)
        self.assertGreater(source.index("quac_action_wait_free(wait)"), source.index("subghz_txrx_stop(txrx)"))

    def test_strict_duration_boundaries(self):
        header = QUAC / "actions/action_duration.h"
        code = header.read_text() if header.exists() else "bool quac_duration_valid(uint32_t);bool quac_duration_parse(const char*,bool,uint32_t*);"
        native('''#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
''' + code.replace("#pragma once", "") + r'''
int main(void){uint32_t value=77;
 assert(!quac_duration_valid(0));assert(!quac_duration_valid(99));
 assert(quac_duration_valid(100));assert(quac_duration_valid(150));assert(quac_duration_valid(60000));
 assert(!quac_duration_valid(60001));assert(!quac_duration_valid(UINT32_MAX));
 assert(quac_duration_parse("150",false,&value)&&value==150);
 assert(quac_duration_parse("60000",false,&value)&&value==60000);
 assert(quac_duration_parse("0",true,&value)&&value==0);
 assert(quac_duration_parse("25",true,&value)&&value==25);
 const char* bad[]={"0","99","60001","4294967296","-1","+150","150x","150 200",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){value=77;assert(!quac_duration_parse(bad[i],false,&value));assert(value==77);}
 return 0;}
''')

    def test_timed_actions_do_not_narrow_duration(self):
        for name in ("nfc", "rfid", "ibutton"):
            source = (QUAC / "actions" / f"action_{name}.c").read_text()
            self.assertNotIn("int16_t time_ms", source)
            self.assertIn("quac_duration_valid", source)
            self.assertIn("quac_action_wait_run", source)
            self.assertIn("quac_action_wait_free", source)

    def test_cancellation_is_not_a_delay_loop(self):
        path = QUAC / "actions/action_wait.c"
        self.assertTrue(path.exists(), "missing cancellable wait adapter")
        source = path.read_text()
        self.assertIn("furi_pubsub_unsubscribe", source)
        self.assertIn("suppress_next_back = true", source)
        self.assertIn("furi_ms_to_ticks", source)
        production = function(source, "static void quac_action_wait_input(")
        production += "\n" + function(source, "bool quac_action_wait_run(")
        native(r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define QUAC_WAIT_CANCEL 1U
#define QUAC_WAIT_DONE 2U
#define FuriWaitForever UINT32_MAX
#define FuriFlagError 0x80000000U
#define FuriFlagErrorTimeout 0xfffffffeU
#define FuriFlagWaitAny 0
enum{InputKeyBack,InputKeyOk,InputTypePress,InputTypeRelease};
typedef struct{int key,type;}InputEvent;
typedef struct{bool action_cancelled,suppress_next_back;}App;
typedef struct{App*app;void*flags;}QuacActionWait;
static uint32_t result,timeout,marked;
static void furi_event_flag_set(void*f,uint32_t v){(void)f;marked|=v;}
static uint32_t furi_ms_to_ticks(uint32_t v){return v*2;}
static uint32_t furi_event_flag_wait(void*f,uint32_t bits,int option,uint32_t ticks){(void)f;(void)bits;(void)option;timeout=ticks;return result;}
''' + production + r'''
int main(void){App app={0};QuacActionWait wait={&app,0};InputEvent event={InputKeyBack,InputTypePress};
 quac_action_wait_input(&event,&wait);assert(marked==1);marked=0;event.type=InputTypeRelease;quac_action_wait_input(&event,&wait);assert(!marked);
 result=FuriFlagErrorTimeout;assert(quac_action_wait_run(&wait,150));assert(timeout==300&&!app.action_cancelled);
 result=QUAC_WAIT_CANCEL;assert(!quac_action_wait_run(&wait,60000));assert(app.action_cancelled&&app.suppress_next_back&&timeout==120000);
 result=QUAC_WAIT_DONE;assert(quac_action_wait_run(&wait,FuriWaitForever));assert(timeout==FuriWaitForever);
 result=FuriFlagError;assert(!quac_action_wait_run(&wait,100));
 result=QUAC_WAIT_CANCEL|QUAC_WAIT_DONE;assert(!quac_action_wait_run(&wait,100));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
