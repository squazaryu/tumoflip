"""Advanced radio/timing editor: native policy and view-ownership regressions."""
from pathlib import Path
import unittest

from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_erreka_868_add_manually import without_includes

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "applications/main/subghz/plugins/add_manually"


class ManualParametersTests(unittest.TestCase):
    def policy(self):
        path = PLUGIN / "subghz_manual_params.h"
        if not path.exists():
            native("unsigned subghz_manual_band_count(void);\n"
                   "int main(void){return subghz_manual_band_count()==0;}\n")
        return without_includes(path.read_text())

    def test_radio_choices_and_timing_limits_are_bounded(self):
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
''' + self.policy() + r'''
int main(void){
 assert(subghz_manual_band_count()==15);
 for(unsigned i=0;i<15;i++){
  const SubGhzManualBand* band=subghz_manual_band_get(i);assert(band);
  assert(!strcmp(band->mod,"AM650")||!strcmp(band->mod,"FM12K")||!strcmp(band->mod,"FM476"));
  assert(band->frequency>=300000000 && band->frequency<=868800000);
 }
 assert(!subghz_manual_band_get(15));assert(!subghz_manual_band_get(255));
 assert(subghz_manual_band_index(315000000,"AM650")==12);
 assert(subghz_manual_band_index(433920000,"FM12K")==1);
 assert(subghz_manual_band_index(1,"TumoHonda")==-1);
 assert(subghz_manual_band_index(433920000,NULL)==-1);
 SubGhzManualTiming t={0};
 assert(subghz_manual_timing("Princeton",&t));assert(t.te_min==109&&t.te_max==689&&t.gt_min==15&&t.gt_max==72);
 assert(subghz_manual_value_valid(109,t.te_min,t.te_max));assert(subghz_manual_value_valid(689,t.te_min,t.te_max));
 assert(!subghz_manual_value_valid(-1,t.te_min,t.te_max));assert(!subghz_manual_value_valid(108,t.te_min,t.te_max));
 assert(!subghz_manual_value_valid(690,t.te_min,t.te_max));assert(!subghz_manual_value_valid(1,0,0));
 assert(subghz_manual_timing("Faac TM",&t));assert(t.te_min==101&&t.te_max==999&&!t.gt_min&&!t.gt_max);
 assert(!subghz_manual_timing("CAME",&t));assert(!t.te_min&&!t.gt_min);
 assert(!subghz_manual_timing(NULL,&t));assert(!subghz_manual_timing("Princeton",NULL));
 return 0;
}
''')

    def test_editor_rejects_bad_values_and_releases_number_view_once(self):
        policy = self.policy()
        path = PLUGIN / "subghz_scene_set_manual_params.c"
        if not path.exists():
            native("void subghz_scene_set_manual_params_on_enter(void*);\n"
                   "int main(void){subghz_scene_set_manual_params_on_enter(0);return 0;}\n")
        source = without_includes(path.read_text())
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {int type;const char* mod;uint32_t freq;struct {const char*name;uint32_t te,gt;}data;}GenInfo;
enum{GenData,GenKeeloq};
typedef struct {unsigned type,event;}SceneManagerEvent;
enum{SceneManagerEventTypeCustom,SceneManagerEventTypeBack};
enum{SubGhzSceneShowError,SubGhzViewIdWidget,SubGhzViewIdVariableItemList,SubGhzViewIdNumberInput};
typedef unsigned View;
typedef struct {View view;void(*callback)(void*,int32_t);void*ctx;int32_t min,max,value;}NumberInput;
typedef struct {unsigned index;const char*text;void*ctx;}VariableItem;
typedef struct {VariableItem items[4];unsigned count;void(*enter)(void*,uint32_t);void*ctx;}VariableItemList;
typedef struct {GenInfo*gen_info;VariableItemList*variable_item_list;void*view_dispatcher,*scene_manager,*txrx,*error_str;}SubGhz;
static unsigned current_view,added,removed,freed,last_event,last_scene,proceeded;
static bool alloc_ok=true,preset_ok=true;
static VariableItem* variable_item_list_add(VariableItemList*l,const char*n,uint8_t count,void(*cb)(VariableItem*),void*ctx){(void)n;(void)count;(void)cb;assert(l->count<4);VariableItem*i=&l->items[l->count++];i->ctx=ctx;return i;}
static void variable_item_list_reset(VariableItemList*l){l->count=0;}
static void variable_item_list_set_enter_callback(VariableItemList*l,void(*cb)(void*,uint32_t),void*ctx){l->enter=cb;l->ctx=ctx;}
static void variable_item_set_current_value_index(VariableItem*i,uint8_t n){i->index=n;}
static uint8_t variable_item_get_current_value_index(VariableItem*i){return i->index;}
static void* variable_item_get_context(VariableItem*i){return i->ctx;}
static void variable_item_set_current_value_text(VariableItem*i,const char*t){i->text=t;}
static void variable_item_list_set_selected_item(VariableItemList*l,uint8_t i){(void)l;(void)i;}
static NumberInput* number_input_alloc(void){return alloc_ok?calloc(1,sizeof(NumberInput)):NULL;}
static void number_input_free(NumberInput*i){freed++;free(i);}
static View* number_input_get_view(NumberInput*i){return &i->view;}
static void number_input_set_header_text(NumberInput*i,const char*t){(void)i;(void)t;}
static void number_input_set_result_callback(NumberInput*i,void(*cb)(void*,int32_t),void*ctx,int32_t v,int32_t lo,int32_t hi){i->callback=cb;i->ctx=ctx;i->value=v;i->min=lo;i->max=hi;}
static void view_set_previous_callback(View*v,uint32_t(*cb)(void*)){(void)v;(void)cb;}
static void view_dispatcher_add_view(void*d,unsigned id,View*v){(void)d;(void)id;(void)v;added++;}
static void view_dispatcher_remove_view(void*d,unsigned id){(void)d;assert(current_view!=id);removed++;}
static void view_dispatcher_switch_to_view(void*d,unsigned id){(void)d;current_view=id;}
static void view_dispatcher_send_custom_event(void*d,unsigned ev){(void)d;last_event=ev;}
static void scene_manager_next_scene(void*s,unsigned id){(void)s;last_scene=id;}
static void furi_string_set(void*s,const char*t){(void)s;(void)t;}
static void* subghz_txrx_get_setting(void*t){return t;}
static int subghz_setting_get_inx_preset_by_name(void*s,const char*n){(void)s;(void)n;return preset_ok?0:-1;}
static bool subghz_scene_set_type_proceed_advanced(SubGhz*s){(void)s;proceeded++;return true;}
''' + policy + source + r'''
int main(void){
 GenInfo g={.type=GenData,.mod="AM650",.freq=433920000,.data={"Princeton",400,30}};
 VariableItemList list={0};SubGhz s={.gen_info=&g,.variable_item_list=&list};
 subghz_scene_set_manual_params_on_enter(&s);assert(list.count==4&&list.enter);
 list.enter(&s,1);assert(added==1&&current_view==SubGhzViewIdNumberInput);
 manual_input->callback(manual_input->ctx,108);assert(g.data.te==400&&last_event==0);
 manual_input->callback(manual_input->ctx,689);assert(last_event==SubGhzManualEventNumberDone);
 assert(subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event}));assert(g.data.te==689);
 list.enter(&s,2);assert(added==1);
 manual_input->callback(manual_input->ctx,72);
 assert(subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event}));assert(g.data.gt==72);
 list.enter(&s,3);assert(last_event==SubGhzManualEventContinue);
 assert(subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event}));assert(proceeded==1);
 assert(!subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,999}));
 subghz_scene_set_manual_params_on_exit(&s);assert(removed==1&&freed==1&&!list.enter&&!manual_input);
 subghz_scene_set_manual_params_on_exit(&s);assert(removed==1&&freed==1);
 subghz_scene_set_manual_params_on_enter(&s);list.enter(&s,1);preset_ok=false;
 subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,SubGhzManualEventContinue});assert(last_scene==SubGhzSceneShowError&&proceeded==1);
 subghz_scene_set_manual_params_on_exit(&s);preset_ok=true;
 alloc_ok=false;subghz_scene_set_manual_params_on_enter(&s);list.enter(&s,1);assert(!manual_input&&last_scene==SubGhzSceneShowError);
 subghz_scene_set_manual_params_on_exit(&s);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
