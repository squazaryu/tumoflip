"""Receive-only modulation gate: actual decode loop and bounded preset parser."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class SubGhzModulationFilterTest(unittest.TestCase):
    def test_receiver_keeps_raw_unknown_and_legacy_callers(self):
        source = (ROOT / "lib/subghz/receiver.c").read_text()
        setter = "void subghz_receiver_set_modulation_filter("
        production = function(source, setter) if setter in source else ""
        production += "\n" + function(source, "void subghz_receiver_decode(")
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define furi_check assert
typedef unsigned SubGhzProtocolFlag;
enum {SubGhzProtocolFlag_AM=1,SubGhzProtocolFlag_FM=2,SubGhzProtocolFlag_Decodable=4,SubGhzProtocolFlag_RAW=8};
typedef struct {void (*feed)(void*,bool,uint32_t);} Decoder;
typedef struct {unsigned flag;Decoder* decoder;} Protocol;
typedef struct {Protocol* protocol;} Base;
typedef struct {Base* base;} Slot;
typedef struct {Slot* slots;unsigned filter,modulation_filter;} SubGhzReceiver;
#define M_EACH(slot,slots,type) (Slot* slot=(slots);slot<(slots)+4;++slot)
void subghz_receiver_set_modulation_filter(SubGhzReceiver*,SubGhzProtocolFlag);
static int calls[4];static Base* bases;
static void feed(void* b,bool level,uint32_t duration){assert(level && duration==300);calls[(Base*)b-bases]++;}
''' + production + r'''
int main(void){
 Decoder d={feed};Protocol p[4]={{5,&d},{6,&d},{7,&d},{8,&d}};
 Base b[4]={{p},{p+1},{p+2},{p+3}};bases=b;Slot slots[4]={{b},{b+1},{b+2},{b+3}};
 SubGhzReceiver r={slots,12,0},other={slots,12,0};
 subghz_receiver_set_modulation_filter(&r,SubGhzProtocolFlag_AM);
 subghz_receiver_decode(&r,true,300);assert(calls[0]==1 && calls[1]==0 && calls[2]==1 && calls[3]==1);
 subghz_receiver_set_modulation_filter(&other,SubGhzProtocolFlag_FM);
 subghz_receiver_decode(&other,true,300);assert(calls[0]==1 && calls[1]==1 && calls[2]==2 && calls[3]==2);
 subghz_receiver_decode(&r,true,300);assert(calls[0]==2 && calls[1]==1); // no global shared state
 subghz_receiver_set_modulation_filter(&r,0);subghz_receiver_decode(&r,true,300);assert(calls[0]==3 && calls[1]==2);
 subghz_receiver_set_modulation_filter(&r,3);subghz_receiver_decode(&r,true,300);assert(calls[0]==4 && calls[1]==3);
 r.filter=8;subghz_receiver_set_modulation_filter(&r,2);subghz_receiver_decode(&r,true,300);assert(calls[0]==4 && calls[1]==3 && calls[3]==6);
 return 0;
}
''')

    def test_preset_data_not_display_name_controls_modulation(self):
        path = ROOT / "lib/subghz/subghz_preset_modulation.h"
        production = path.read_text() if path.exists() else "unsigned subghz_preset_modulation(const char*,const uint8_t*,size_t);"
        production = "\n".join(line for line in production.splitlines()
                               if not line.startswith(("#pragma once", '#include "')))
        native(r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef unsigned SubGhzProtocolFlag;
enum {SubGhzProtocolFlag_AM=1,SubGhzProtocolFlag_FM=2};
''' + production + r'''
int main(void){
 uint8_t preset[12]={0x12,0x30,0,0,0,0,0,0,0,0,0,0};
 assert(subghz_preset_modulation("FM476",preset,sizeof(preset))==1);
 preset[1]=0x00;assert(subghz_preset_modulation("AM650",preset,sizeof(preset))==2);
 preset[1]=0x10;assert(subghz_preset_modulation("CUSTOM",preset,sizeof(preset))==2);
 preset[1]=0x40;assert(subghz_preset_modulation("CUSTOM",preset,sizeof(preset))==2);
 preset[1]=0x70;assert(subghz_preset_modulation("CUSTOM",preset,sizeof(preset))==2);
 preset[1]=0x20;assert(subghz_preset_modulation("AM650",preset,sizeof(preset))==0);
 for(size_t n=0;n<sizeof(preset);n++)assert(subghz_preset_modulation("AM650",preset,n)==0);
 preset[0]=0x11;assert(subghz_preset_modulation("CUSTOM",preset,sizeof(preset))==0);
 assert(subghz_preset_modulation("AM270",NULL,0)==1);
 assert(subghz_preset_modulation("AM650",NULL,0)==1);
 assert(subghz_preset_modulation("FM238",NULL,0)==2);
 assert(subghz_preset_modulation("FM476",NULL,0)==2);
 assert(subghz_preset_modulation("FM12K",NULL,0)==2);
 assert(subghz_preset_modulation("AM-custom",NULL,0)==0);
 assert(subghz_preset_modulation(NULL,NULL,0)==0);
 return 0;
}
''')

    def test_standard_arf_and_new_receivers_use_the_same_preset_rule(self):
        for app in ("applications/main/subghz", "applications_user/arf_subghz_full"):
            source = (ROOT / app / "helpers/subghz_txrx.c").read_text()
            setter = function(source, "void subghz_txrx_set_preset(")
            self.assertIn("subghz_receiver_set_modulation_filter", setter)
            self.assertIn("subghz_preset_modulation", source)
            self.assertIn("subghz_preset_modulation", function(source,
                          "static void subghz_txrx_configure_receiver(")) if app.startswith("applications/main") else self.assertGreaterEqual(source.count("subghz_preset_modulation("), 3)
        core = (ROOT / "applications/main/subghz/helpers/subghz_txrx.c").read_text()
        self.assertIn("diversity_receiver", function(core, "void subghz_txrx_set_preset("))
        api = (ROOT / "targets/f7/api_symbols.csv").read_text()
        self.assertIn("Function,+,subghz_receiver_set_modulation_filter", api)


if __name__ == "__main__":
    unittest.main()
