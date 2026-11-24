"""History labels and pending notification retention, without RF or GUI I/O."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_erreka_868_add_manually import without_includes

ROOT = Path(__file__).resolve().parents[2]


class ReceiveLabelsTests(unittest.TestCase):
    def test_ring_pruning_preserves_a_pending_rx_done(self):
        for relative in ("applications/main/subghz/scenes/subghz_scene_receiver.c",
                         "applications_user/arf_subghz_full/scenes/subghz_scene_receiver.c"):
            source = (ROOT / relative).read_text()
            start = source.index("if(subghz_history_get_last_index(subghz->history) >= 54) {")
            end = source.index("subghz_view_receiver_disable_draw_callback", start)
            branch = source[start:end] + "}\n"
            native(r'''
#include <assert.h>
enum{SubGhzNotificationStateIDLE,SubGhzNotificationStateRx,SubGhzNotificationStateRxDone,SubGhzNotificationStateTxWait};
typedef struct{void*history;unsigned state_notifications;}SubGhz;
static unsigned count=54;
static unsigned subghz_history_get_last_index(void*h){(void)h;return count;}
static void prune(SubGhz*subghz){
''' + branch + r'''
}
int main(void){
 for(unsigned state=0;state<4;state++){
  SubGhz g={.state_notifications=state};prune(&g);
  assert(g.state_notifications==(state==SubGhzNotificationStateIDLE?SubGhzNotificationStateRx:state));
 }
 count=53;SubGhz g={.state_notifications=SubGhzNotificationStateIDLE};prune(&g);assert(g.state_notifications==SubGhzNotificationStateIDLE);
 return 0;
}
''')

    def test_labels_follow_valid_saved_variants_without_changing_protocol_identity(self):
        path = ROOT / "lib/subghz/protocols/history_name.h"
        if not path.exists():
            native("void subghz_history_name_variant(void*,void*,void*);\n"
                   "int main(void){subghz_history_name_variant(0,0,0);return 0;}\n")
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define KEELOQ_TE_FAST 200u
typedef struct{char text[64];}FuriString;
typedef struct{unsigned bits,no_ic;const char*manufacturer;bool ic,has_no_ic,bad_rewind;}FlipperFormat;
static const char*furi_string_get_cstr(FuriString*s){return s->text;}
static void furi_string_set_str(FuriString*s,const char*t){snprintf(s->text,sizeof(s->text),"%s",t);}
static void furi_string_set(FuriString*s,FuriString*t){*s=*t;}
static bool furi_string_empty(FuriString*s){return !s->text[0];}
static void furi_string_printf(FuriString*s,const char*fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(s->text,sizeof(s->text),fmt,ap);va_end(ap);}
static bool flipper_format_rewind(FlipperFormat*f){return !f->bad_rewind;}
static bool flipper_format_read_uint32(FlipperFormat*f,const char*k,uint32_t*v,unsigned n){assert(n==1);if(!strcmp(k,"Bit")){*v=f->bits;return true;}*v=f->no_ic;return f->has_no_ic;}
static bool flipper_format_read_string(FlipperFormat*f,const char*k,FuriString*s){assert(!strcmp(k,"Manufacture"));if(!f->manufacturer)return false;furi_string_set_str(s,f->manufacturer);return true;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*v,unsigned n){assert(!strcmp(k,"IC")&&n==2);(void)v;return f->ic;}
''' + without_includes(path.read_text()) + r'''
static void check(const char*protocol,const char*expected,FlipperFormat f){FuriString name={0},scratch={0};furi_string_set_str(&name,protocol);subghz_history_name_variant(&name,&f,&scratch);assert(!strcmp(name.text,expected));}
int main(void){
 check("CAME","Prastel",(FlipperFormat){.bits=25});check("CAME","Airforce",(FlipperFormat){.bits=18});
 check("CAME","CAME",(FlipperFormat){.bits=42});check("Prastel","Prastel",(FlipperFormat){.bits=42});
 check("Faac SLH","Genius SLH",(FlipperFormat){.manufacturer="Genius SLH"});
 check("Faac SLH","Faac SLH",(FlipperFormat){.manufacturer="FAAC_SLH"});check("Faac SLH","Faac SLH",(FlipperFormat){0});
 check("Nice FloR-S","Nice One",(FlipperFormat){.bits=72});
 check("Nice FloR-S","Nice O-Code",(FlipperFormat){.bits=52,.ic=true});
 check("Nice FloR-S","Nice O-Code",(FlipperFormat){.bits=72,.has_no_ic=true,.no_ic=1});
 check("Nice FloR-S","Nice FloR-S",(FlipperFormat){.bits=52,.has_no_ic=true,.no_ic=2});
 check("Nice FloR-S","Nice FloR-S",(FlipperFormat){.bits=52});
 check("Nice FloR-S","Nice FloR-S",(FlipperFormat){.bits=72,.bad_rewind=true});
 check("BelKorn","BelKorn",(FlipperFormat){.bits=118});
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
