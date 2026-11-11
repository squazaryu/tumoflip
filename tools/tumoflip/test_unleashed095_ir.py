"""Real IR parser/CLI regressions, with no hardware or filesystem writes."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


def error_header():
    text = (ROOT / 'lib/infrared/signal/infrared_error_code.h').read_text()
    return re.sub(r'^#pragma once\n|^#ifdef __cplusplus\nextern "C" \{\n#endif\n|'
                  r'^#ifdef __cplusplus\n}\n#endif\n', '', text, flags=re.M)


class Infrared095Tests(unittest.TestCase):
    def test_empty_raw_cannot_reuse_the_previous_signal(self):
        source = (ROOT / 'lib/infrared/signal/infrared_signal.c').read_text()
        actual = function(source, 'static inline InfraredErrorCode\n    infrared_signal_read_raw(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define INFRARED_SIGNAL_FREQUENCY_KEY "frequency"
#define INFRARED_SIGNAL_DUTY_CYCLE_KEY "duty"
#define INFRARED_SIGNAL_DATA_KEY "data"
#define MAX_TIMINGS_AMOUNT 1024
typedef struct {unsigned count,step,fail;} FlipperFormat;
typedef struct {unsigned old_value,writes;} InfraredSignal;
static bool flipper_format_read_uint32(FlipperFormat* f,const char* k,uint32_t* v,unsigned n){
    if(++f->step==f->fail)return false;
    if(k[0]=='f')*v=38000;else for(unsigned i=0;i<n;i++)v[i]=i+1;
    return true;
}
static bool flipper_format_read_float(FlipperFormat* f,const char* k,float* v,unsigned n){
    (void)k;assert(n==1);*v=0.33f;return ++f->step!=f->fail;
}
static bool flipper_format_get_value_count(FlipperFormat* f,const char* k,uint32_t* n){
    (void)k;*n=f->count;return ++f->step!=f->fail;
}
static void infrared_signal_set_raw_signal(InfraredSignal* s,const uint32_t* t,unsigned n,uint32_t freq,float duty){
    assert(freq==38000 && duty>0);if(n){assert(t[0]==1);s->old_value=t[0];s->writes++;}
}
''' + error_header() + actual + r'''
int main(void){
    InfraredSignal s={.old_value=99};FlipperFormat f={0};
    assert(infrared_signal_read_raw(&s,&f)!=InfraredErrorCodeNone);
    assert(s.old_value==99 && !s.writes);
    f=(FlipperFormat){.count=1025};assert(infrared_signal_read_raw(&s,&f)!=InfraredErrorCodeNone);
    for(unsigned fail=1;fail<=4;fail++){
        f=(FlipperFormat){.count=3,.fail=fail};assert(infrared_signal_read_raw(&s,&f)!=InfraredErrorCodeNone);
        assert(s.old_value==99 && !s.writes);
    }
    f=(FlipperFormat){.count=3};assert(infrared_signal_read_raw(&s,&f)==InfraredErrorCodeNone);
    assert(s.old_value==1 && s.writes==1);return 0;
}
''')

    def test_cli_does_not_stop_an_already_closed_database(self):
        actual = function((ROOT / 'applications/main/infrared/infrared_cli.c').read_text(),
                          'static void infrared_cli_brute_force_signals(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
static int host_printf(const char* format,...){(void)format;return 0;}
#define printf host_printf
#define INFRARED_ASSETS_FOLDER "/ext/infrared/assets"
#define INFRARED_BRUTE_FORCE_DUMMY_INDEX 0
#define InfraredErrorCodeNone 0
typedef int PipeSide;
typedef struct {const char* text;} FuriString;
typedef struct {bool started;} InfraredBruteForce;
static InfraredBruteForce state;
static bool can_start=true,cancel;
static unsigned stops,signals=2,sends;
static InfraredBruteForce* infrared_brute_force_alloc(void){state.started=false;return &state;}
static FuriString* furi_string_alloc_printf(const char* fmt,...){(void)fmt;static FuriString s={"fake.ir"};return &s;}
static const char* furi_string_get_cstr(FuriString* s){return s->text;}
static bool furi_string_empty(FuriString* s){return !s->text[0];}
static void furi_string_free(FuriString* s){(void)s;}
static void infrared_brute_force_set_db_filename(InfraredBruteForce* b,const char* p){(void)b;(void)p;}
static void infrared_brute_force_add_record(InfraredBruteForce* b,unsigned i,const char* p){(void)b;(void)i;(void)p;}
static unsigned infrared_brute_force_calculate_messages(InfraredBruteForce* b){(void)b;return 0;}
static bool infrared_brute_force_start(InfraredBruteForce* b,unsigned i,uint32_t* n){(void)i;*n=signals;b->started=can_start&&signals;return b->started;}
static bool infrared_brute_force_is_started(InfraredBruteForce* b){return b->started;}
static bool infrared_brute_force_send(InfraredBruteForce* b,unsigned i){assert(b->started);sends++;return i<signals;}
static bool cli_is_pipe_broken_or_is_etx_next_char(PipeSide* p){(void)p;return cancel;}
static void infrared_brute_force_stop(InfraredBruteForce* b){assert(b->started);b->started=false;stops++;}
static void infrared_brute_force_reset(InfraredBruteForce* b){assert(!b->started);}
static void infrared_brute_force_free(InfraredBruteForce* b){assert(!b->started);}
''' + actual + r'''
int main(void){
    (void)infrared_brute_force_is_started;
    PipeSide p=0;FuriString remote={"tv"},name={"Power"};
    can_start=false;infrared_cli_brute_force_signals(&p,&remote,&name);
    assert(stops==0 && sends==0);
    can_start=true;cancel=true;infrared_cli_brute_force_signals(&p,&remote,&name);assert(stops==1 && sends==1);
    cancel=false;signals=0;infrared_cli_brute_force_signals(&p,&remote,&name);assert(stops==1);
    name.text="";infrared_cli_brute_force_signals(&p,&remote,&name);assert(stops==1);return 0;
}
''')

    def test_reset_discards_borrowed_database_path(self):
        actual = function((ROOT / 'lib/infrared/signal/infrared_brute_force.c').read_text(),
                          'void infrared_brute_force_reset(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_check assert
#define furi_assert assert
typedef struct {const char* db_filename;bool is_started;int records;} InfraredBruteForce;
#define InfraredBruteForceRecordDict_reset(r) ((r)=0)
''' + actual + r'''
int main(void){InfraredBruteForce b={.db_filename="borrowed.ir",.records=4};
 infrared_brute_force_reset(&b);assert(b.db_filename==NULL && !b.records);return 0;}
''')


if __name__ == '__main__':
    unittest.main()
