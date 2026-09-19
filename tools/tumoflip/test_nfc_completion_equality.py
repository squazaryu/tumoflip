"""Native regressions for content equality and already-completed NFC reads."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
NFC = ROOT / "lib/nfc/protocols"
SCENE = ROOT / "applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic_extra_scenes.c"


def native(body):
    with tempfile.TemporaryDirectory(prefix="nfc-completion-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(body)
        result = subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-O1", "-g",
                                 "-fsanitize=address,undefined", str(path / "test.c"),
                                 "-o", str(path / "test")], text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(path / "test")], text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stderr)


class NfcCompletionEqualityTest(unittest.TestCase):
    def test_nested_array_content_equality(self):
        simple = (ROOT / "lib/toolbox/simple_array.c").read_text()
        felica = (NFC / "felica/felica_i.c").read_text()
        desfire = (NFC / "mf_desfire/mf_desfire_i.c").read_text()
        # Missing helpers are deliberate link-time RED, not broken test dependencies.
        helpers = ""
        for source, signatures in (
            (felica, ("static bool felica_system_is_equal(", "bool felica_system_array_is_equal(")),
            (desfire, ("static bool\n    mf_desfire_file_data_array_is_equal(",
                       "static bool mf_desfire_application_is_equal(",
                       "bool mf_desfire_application_array_is_equal(")),
        ):
            for signature in signatures:
                if signature in source:
                    helpers += function(source, signature) + "\n"
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define furi_check assert
typedef struct { size_t type_size; } SimpleArrayConfig;
typedef struct { const SimpleArrayConfig* config; void* data; uint32_t count; } SimpleArray;
static uint32_t __attribute__((unused)) simple_array_get_count(const SimpleArray* a){return a->count;}
static const void* __attribute__((unused)) simple_array_cget(const SimpleArray* a,uint32_t i){assert(i<a->count);return (char*)a->data+i*a->config->type_size;}
typedef struct {uint8_t system_code_idx;uint16_t system_code,key_version;SimpleArray *services,*areas,*public_blocks;} FelicaSystem;
typedef struct {bool is_master_key_changeable,is_free_directory_list,is_free_create_delete,is_config_changeable;uint8_t change_key_id,max_keys,flags;} MfDesfireKeySettings;
typedef struct {SimpleArray* data;} MfDesfireFileData;
typedef struct {MfDesfireKeySettings key_settings;SimpleArray *key_versions,*file_ids,*file_settings,*file_data;} MfDesfireApplication;
bool felica_system_array_is_equal(const SimpleArray*,const SimpleArray*);
bool mf_desfire_application_array_is_equal(const SimpleArray*,const SimpleArray*);
''' + function(simple, "bool simple_array_is_equal(") + "\n" + helpers + r'''
int main(void){
 const SimpleArrayConfig byte={1}, fc={sizeof(FelicaSystem)}, dc={sizeof(MfDesfireApplication)}, fd={sizeof(MfDesfireFileData)};
 uint8_t x[3]={1,2,3},y[3]={1,2,3};
 SimpleArray a={&byte,x,3},b={&byte,y,3},empty={&byte,NULL,0};
 FelicaSystem fa[2]={{.system_code=7,.services=&a,.areas=&a,.public_blocks=&a},{.services=&empty,.areas=&empty,.public_blocks=&empty}};
 FelicaSystem fb[2]={{.system_code=7,.services=&b,.areas=&b,.public_blocks=&b},{.services=&empty,.areas=&empty,.public_blocks=&empty}};
 SimpleArray f1={&fc,fa,2},f2={&fc,fb,2};
 assert(felica_system_array_is_equal(&f1,&f2));
 y[2]=4;assert(!felica_system_array_is_equal(&f1,&f2));y[2]=3;
 fb[1].key_version=1;assert(!felica_system_array_is_equal(&f1,&f2));fb[1].key_version=0;
 fb[1].system_code_idx=1;assert(!felica_system_array_is_equal(&f1,&f2));fb[1].system_code_idx=0;
 fb[1].system_code=1;assert(!felica_system_array_is_equal(&f1,&f2));fb[1].system_code=0;
 f2.count=1;assert(!felica_system_array_is_equal(&f1,&f2));f1.count=f2.count=0;assert(felica_system_array_is_equal(&f1,&f2));
 MfDesfireFileData da[2]={{&a},{&empty}},db[2]={{&b},{&empty}};
 SimpleArray d1={&fd,da,2},d2={&fd,db,2};
 MfDesfireApplication ca={.key_versions=&a,.file_ids=&a,.file_settings=&a,.file_data=&d1};
 MfDesfireApplication cb={.key_versions=&b,.file_ids=&b,.file_settings=&b,.file_data=&d2};
 SimpleArray c1={&dc,&ca,1},c2={&dc,&cb,1};
 assert(mf_desfire_application_array_is_equal(&c1,&c2));
 cb.key_settings.flags=1;assert(!mf_desfire_application_array_is_equal(&c1,&c2));cb.key_settings.flags=0;
 cb.key_versions=&empty;assert(!mf_desfire_application_array_is_equal(&c1,&c2));cb.key_versions=&b;
 cb.file_ids=&empty;assert(!mf_desfire_application_array_is_equal(&c1,&c2));cb.file_ids=&b;
 cb.file_settings=&empty;assert(!mf_desfire_application_array_is_equal(&c1,&c2));cb.file_settings=&b;
 db[1].data=&b;assert(!mf_desfire_application_array_is_equal(&c1,&c2));db[1].data=&empty;
 d2.count=1;assert(!mf_desfire_application_array_is_equal(&c1,&c2));d2.count=2;
 y[2]=4;assert(!mf_desfire_application_array_is_equal(&c1,&c2));y[2]=3;
 c2.count=0;assert(!mf_desfire_application_array_is_equal(&c1,&c2));c1.count=0;assert(mf_desfire_application_array_is_equal(&c1,&c2));
 return 0;
}
''')
        for protocol, field, helper in (("felica", "systems", "felica_system_array_is_equal"),
                                        ("mf_desfire", "applications", "mf_desfire_application_array_is_equal")):
            source = (NFC / protocol / f"{protocol}.c").read_text()
            self.assertIn(f"{helper}(data->{field}, other->{field})", source)

    def test_complete_card_never_requests_more_keys(self):
        source = (NFC / "mf_classic/mf_classic_poller.c").read_text()
        helper = "static bool mf_classic_poller_is_card_read("
        production = function(source, helper) if helper in source else ""
        production += "\n" + function(source, "NfcCommand mf_classic_poller_handler_request_key(")
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define FURI_LOG_D(...) ((void)0)
typedef int NfcCommand;
enum {NfcCommandContinue,MfClassicPollerEventTypeRequestKey,MfClassicPollerModeDictAttackCUID,MfClassicKeyTypeB,MfClassicPollerStateAuthKeyB,MfClassicPollerStateAuthKeyA,MfClassicPollerStateNextSector,MfClassicPollerStateSuccess};
typedef struct {uint8_t sectors,keys;} MfClassicData;
typedef struct {int current_key,requested_key_type,mode;} MfClassicPollerDictAttackContext;
typedef struct {struct {MfClassicPollerDictAttackContext dict_attack_ctx;} mode_ctx;struct {int type;} mfc_event;
 struct {struct {bool key_provided;int key,key_type;} key_request_data;} mfc_event_data;
 int state,general_event;void* context;NfcCommand (*callback)(int,void*);MfClassicData* data;uint8_t sectors_total;} MfClassicPoller;
static void __attribute__((unused)) mf_classic_get_read_sectors_and_keys(MfClassicData* d,uint8_t* s,uint8_t* k){*s=d->sectors;*k=d->keys;}
static int requests;
static NfcCommand callback(int e,void* c){(void)e;(void)c;requests++;return NfcCommandContinue;}
''' + production + r'''
int main(void){
 MfClassicData d={16,32};MfClassicPoller p={.callback=callback,.data=&d,.sectors_total=16};
 mf_classic_poller_handler_request_key(&p);assert(requests==0 && p.state==MfClassicPollerStateSuccess);
 d.sectors=15;mf_classic_poller_handler_request_key(&p);assert(requests==1 && p.state==MfClassicPollerStateNextSector);
 d.sectors=16;d.keys=31;mf_classic_poller_handler_request_key(&p);assert(requests==2);
 d.keys=32;p.sectors_total=40;mf_classic_poller_handler_request_key(&p);assert(requests==3);
 p.mfc_event_data.key_request_data.key_provided=true;p.mfc_event_data.key_request_data.key_type=MfClassicKeyTypeB;p.mode_ctx.dict_attack_ctx.mode=MfClassicPollerModeDictAttackCUID;
 mf_classic_poller_handler_request_key(&p);assert(requests==4 && p.state==MfClassicPollerStateAuthKeyB);
 return 0;
}
''')

    def test_scene_and_nested_entry_check_completion(self):
        scene = SCENE.read_text()
        handler = function(scene, "static bool mf_classic_scene_dict_attack_on_event(")
        self.assertIn("&& !card_read", handler)
        self.assertEqual(handler.count("bool card_read ="), 2)
        self.assertEqual(handler.count("is_card_present && !card_read"), 2)
        poller = (NFC / "mf_classic/mf_classic_poller.c").read_text()
        nested = function(poller, "NfcCommand mf_classic_poller_handler_nested_controller(")
        self.assertLess(nested.index("mf_classic_poller_is_card_read"), nested.index("auth_passed = true"))
        self.assertIn("poller_has_card_data", handler)


if __name__ == "__main__":
    unittest.main()
