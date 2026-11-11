"""Saved-source ownership must not alias Add Manually's selected submenu row."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
SAVE = ROOT / 'applications/main/subghz/scenes/subghz_scene_save_name.c'


class SaveSourceTests(unittest.TestCase):
    def test_release_and_pr_gates_include_the_new_regressions(self):
        for name in ('pr-build.yml','release.yml'):
            source=(ROOT/'.github/workflows'/name).read_text()
            for module in ('test_unleashed095_ir','test_unleashed095_save','test_unleashed095_rename',
                           'test_unleashed095_browser','test_proteco'):
                self.assertIn('tools.tumoflip.'+module,source)

    def run_save_cases(self, failure_first=False, source_path=SAVE):
        source = source_path.read_text()
        actual = function(source, 'bool subghz_scene_save_name_on_event(')
        actual += '\n' + function(source, 'void subghz_scene_save_name_on_exit(')
        # Distinguish the two production bodies in coverage instead of merging
        # different functions under their common C identifier.
        alias = '#define CHECK_DEED\n' if source_path == SAVE else (
            '#define subghz_scene_save_name_on_event arf_subghz_scene_save_name_on_event\n'
            '#define subghz_scene_save_name_on_exit arf_subghz_scene_save_name_on_exit\n')
        native(alias + ('#define CHECK_FAILURE_FIRST\n' if failure_first else '') + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
enum {SceneManagerEventTypeBack,SceneManagerEventTypeCustom};
enum {SubGhzSceneSetType,SubGhzSceneSaveName,SubGhzSceneReadRAW,SubGhzSceneDecodeRAW,
 SubGhzSceneSaveSuccess,SubGhzSceneSavedMenu,SubGhzSceneMoreRAW,SubGhzSceneShowErrorSub};
enum {SubGhzCustomEventManagerNoSet,SubGhzCustomEventManagerSet,SubGhzCustomEventSceneSaveName};
enum {DolphinDeedSubGhzAddManually,DolphinDeedSubGhzSave};
#define SUBGHZ_APP_FOLDER "/ext/subghz"
#define SUBGHZ_APP_FILENAME_EXTENSION ".sub"
typedef struct {const char* text;} FuriString;
typedef struct {unsigned type,event;} SceneManagerEvent;
typedef struct {void* scene_manager;FuriString *file_path,*file_path_tmp,*error_str;
 char file_name_tmp[32];void *txrx,*history,*text_input;unsigned idx_menu_chosen;} SubGhz;
typedef int FlipperFormat;
static unsigned states[8],next_scene=99,saved_source;
static unsigned last_deed;
static bool can_save=true,rename;
static SubGhz* active;
void subghz_scene_save_name_on_exit(void* context);
static uint32_t scene_manager_get_scene_state(void* s,unsigned id){(void)s;return states[id];}
static void scene_manager_set_scene_state(void* s,unsigned id,unsigned v){(void)s;states[id]=v;}
static bool scene_manager_has_previous_scene(void* s,unsigned id){(void)s;return id==SubGhzSceneSetType;}
static void scene_manager_previous_scene(void* s){(void)s;}
static void scene_manager_next_scene(void* s,unsigned id){(void)s;next_scene=id;subghz_scene_save_name_on_exit(active);}
#define furi_string_set(d,s) ((void)(d),(void)(s))
static const char* furi_string_get_cstr(FuriString* s){return s->text;}
static void furi_string_cat_printf(FuriString* s,const char* f,...){(void)s;(void)f;}
static bool subghz_path_is_file(FuriString* s){(void)s;return rename;}
static bool subghz_rename_file(SubGhz* s){(void)s;return can_save;}
static FlipperFormat* subghz_txrx_get_fff_data(void* t){(void)t;return (void*)1;}
static FlipperFormat* subghz_history_get_raw_data(void* t,unsigned i){(void)t;(void)i;return (void*)2;}
static bool subghz_save_protocol_to_file(SubGhz* s,FlipperFormat* f,const char* p){(void)s;(void)p;saved_source=(uintptr_t)f;return can_save;}
static const char* subghz_txrx_radio_device_get_name(void* t){(void)t;return "internal";}
static void subghz_protocol_raw_gen_fff_data(void* f,const char* p,const char* n){(void)f;(void)p;(void)n;}
static void subghz_file_name_clear(SubGhz* s){(void)s;}
static void dolphin_deed(unsigned d){last_deed=d;}
static void* text_input_get_validator_callback_context(void* t){return t;}
static void text_input_set_validator(void* t,void* a,void* b){(void)t;(void)a;(void)b;}
static void validator_is_file_free(void* t){(void)t;}
static void text_input_reset(void* t){(void)t;}
''' + actual + r'''
int main(void){
 FuriString path={"/ext/subghz"},tmp={""},err={""};
 SubGhz s={.file_path=&path,.file_path_tmp=&tmp,.error_str=&err};strcpy(s.file_name_tmp,"owned");
 active=&s;(void)last_deed;
 SceneManagerEvent ev={SceneManagerEventTypeCustom,SubGhzCustomEventSceneSaveName};
#ifdef CHECK_FAILURE_FIRST
 can_save=false;assert(subghz_scene_save_name_on_event(&s,ev));
 assert(next_scene!=SubGhzSceneSaveSuccess);return 0;
#endif
 states[SubGhzSceneSetType]=17;states[SubGhzSceneSaveName]=SubGhzCustomEventManagerNoSet;
 assert(subghz_scene_save_name_on_event(&s,ev));assert(saved_source==2 && states[SubGhzSceneSetType]==17);
 states[SubGhzSceneSetType]=0;states[SubGhzSceneSaveName]=SubGhzCustomEventManagerSet;
 assert(subghz_scene_save_name_on_event(&s,ev));assert(saved_source==1);
#ifdef CHECK_DEED
 assert(last_deed==DolphinDeedSubGhzAddManually);
#endif
 can_save=false;next_scene=99;assert(subghz_scene_save_name_on_event(&s,ev));
 assert(next_scene!=SubGhzSceneSaveSuccess);
 states[SubGhzSceneSetType]=123;subghz_scene_save_name_on_exit(&s);
 assert(states[SubGhzSceneSaveName]==SubGhzCustomEventManagerNoSet && states[SubGhzSceneSetType]==123);
 can_save=true;rename=true;assert(subghz_scene_save_name_on_event(&s,ev));
 rename=false;ev.type=SceneManagerEventTypeBack;assert(subghz_scene_save_name_on_event(&s,ev));
 ev.type=SceneManagerEventTypeCustom;s.file_name_tmp[0]=0;assert(subghz_scene_save_name_on_event(&s,ev));
 ev.event=999;assert(!subghz_scene_save_name_on_event(&s,ev));return 0;
}
''')

    def test_history_generated_and_canceled_saves_use_separate_state(self):
        self.run_save_cases()

    def test_write_failure_cannot_show_save_success(self):
        self.run_save_cases(failure_first=True)

    def test_arf_uses_the_same_generated_data_contract(self):
        self.run_save_cases(source_path=ROOT/'applications_user/arf_subghz_full/scenes/subghz_scene_save_name.c')

    def test_arf_write_failure_cannot_show_save_success(self):
        self.run_save_cases(failure_first=True,source_path=ROOT/'applications_user/arf_subghz_full/scenes/subghz_scene_save_name.c')

    def test_all_generators_and_private_plugin_version_use_the_new_contract(self):
        base = ROOT / 'applications/main/subghz/plugins/add_manually'
        for name in ('subghz_scene_set_type.c','subghz_scene_set_key.c',
                     'subghz_scene_set_counter.c','subghz_scene_set_seed.c'):
            source=(base/name).read_text()
            self.assertRegex(source, r'scene_manager_set_scene_state\(\s*subghz->scene_manager,\s*'
                             r'SubGhzSceneSaveName,\s*SubGhzCustomEventManagerSet\)')
            self.assertNotRegex(source, r'SubGhzSceneSetType,\s*SubGhzCustomEventManagerSet\)')
        self.assertIn('SUBGHZ_FEATURE_PLUGIN_API_VERSION 1004',
            (ROOT/'applications/main/subghz/helpers/subghz_feature_plugin.h').read_text())


if __name__ == '__main__':
    unittest.main()
