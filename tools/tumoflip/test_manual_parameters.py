"""Advanced radio/timing editor: native policy and view-ownership regressions."""
from pathlib import Path
import unittest

from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_erreka_868_add_manually import without_includes
from tools.tumoflip.test_hotplug_assets import function
import re

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "applications/main/subghz/plugins/add_manually"


class ManualParametersTests(unittest.TestCase):
    def test_te_factory_accepts_faac_without_princeton_only_guard_time(self):
        common = function((ROOT / "lib/subghz/protocols/common.c").read_text(),
                          "SubGhzProtocolStatus subghz_protocol_decoder_common_serialize_te(")
        princeton = function((ROOT / "lib/subghz/protocols/princeton.c").read_text(),
                             "SubGhzProtocolStatus subghz_protocol_decoder_princeton_serialize(")
        faac = (ROOT / "lib/subghz/protocols/faac_tm.c").read_text()
        self.assertIn(".serialize = subghz_protocol_decoder_common_serialize_te,", faac)
        for relative in ("applications/main/subghz/plugins/add_manually/subghz_txrx_create_protocol_key.c",
                         "applications_user/arf_subghz_full/helpers/subghz_txrx_create_protocol_key.c"):
            helper = function((ROOT / relative).read_text(), "bool subghz_txrx_gen_data_protocol_and_te(")
            native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
#define SUBGHZ_PROTOCOL_FAAC_TM_NAME "Faac TM"
typedef enum{SubGhzProtocolStatusOk,SubGhzProtocolStatusErrorParserTe,SubGhzProtocolStatusErrorParserOthers}SubGhzProtocolStatus;
typedef struct{const char*protocol_name;}SubGhzBlockGeneric;
typedef struct{SubGhzBlockGeneric generic;}SubGhzProtocolDecoderCommon;
typedef struct{SubGhzProtocolDecoderCommon common;uint32_t te;}SubGhzProtocolDecoderCommonTe;
typedef struct{SubGhzProtocolDecoderCommon common;uint32_t te,last_data,guard_time;}SubGhzProtocolDecoderPrinceton;
typedef void SubGhzRadioPreset;
typedef struct{bool has_te,has_guard,fail_gen,fail_te,fail_guard;uint32_t te,guard;unsigned guard_updates;}FlipperFormat;
typedef struct{FlipperFormat*fff_data;}SubGhzTxRx;
static SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric*g,FlipperFormat*f,SubGhzRadioPreset*p){(void)g;(void)p;f->has_te=f->has_guard=false;return f->fail_gen?SubGhzProtocolStatusErrorParserOthers:SubGhzProtocolStatusOk;}
static bool flipper_format_write_uint32(FlipperFormat*f,const char*k,const uint32_t*v,unsigned n){assert(n==1);if(!strcmp(k,"TE")){f->has_te=true;f->te=*v;}else{assert(!strcmp(k,"Guard_time"));f->has_guard=true;f->guard=*v;}return true;}
static bool flipper_format_update_uint32(FlipperFormat*f,const char*k,const uint32_t*v,unsigned n){assert(n==1);if(!strcmp(k,"TE")){if(!f->has_te||f->fail_te)return false;f->te=*v;}else{assert(!strcmp(k,"Guard_time"));f->guard_updates++;if(!f->has_guard||f->fail_guard)return false;f->guard=*v;}return true;}
''' + common + princeton + r'''
static bool subghz_txrx_gen_data_protocol(SubGhzTxRx*s,const char*m,uint32_t freq,const char*name,uint64_t key,uint32_t bits){(void)m;(void)freq;(void)key;(void)bits;
 SubGhzProtocolDecoderPrinceton decoder={.common.generic.protocol_name=name,.te=400,.guard_time=30};
 return (!strcmp(name,SUBGHZ_PROTOCOL_FAAC_TM_NAME)?subghz_protocol_decoder_common_serialize_te(&decoder,s->fff_data,NULL):subghz_protocol_decoder_princeton_serialize(&decoder,s->fff_data,NULL))==SubGhzProtocolStatusOk;
}
''' + helper + r'''
int main(void){
 FlipperFormat f={0};SubGhzTxRx s={&f};
 assert(subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Faac TM",0x123,12,999));
 assert(f.has_te&&f.te==999&&!f.has_guard&&!f.guard_updates);
 assert(subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Princeton",0x123,24,689));
 assert(f.has_guard&&f.te==689&&f.guard==30&&f.guard_updates==1);
 f.fail_guard=true;assert(!subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Princeton",0x123,24,400));
 assert(subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Faac TM",0x123,12,101));
 assert(!f.has_guard&&f.te==101&&f.guard_updates==2);
 f.fail_te=true;assert(!subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Faac TM",0x123,12,400));
 f.fail_gen=true;f.fail_te=false;assert(!subghz_txrx_gen_data_protocol_and_te(&s,"AM650",433920000,"Faac TM",0x123,12,400));
 return 0;
}
''')

    def test_guard_time_storage_getter_is_exported_only_by_the_feature_host(self):
        private = (ROOT / "applications/main/subghz/api/subghz_app_api_table_i.h").read_text()
        public = (ROOT / "targets/f7/api_symbols.csv").read_text()
        self.assertRegex(private, r"API_METHOD\(subghz_txrx_get_fff_data,\s*FlipperFormat\*,\s*\(SubGhzTxRx\*\)\)")
        self.assertNotIn("Function,+,subghz_txrx_get_fff_data,", public)

    def test_selected_guard_time_is_persisted_and_write_failures_abort(self):
        source = (PLUGIN / "subghz_scene_set_type.c").read_text()
        generate = function(source, "bool subghz_scene_set_type_generate_protocol_from_infos(")
        branch = generate[generate.index("case GenData:"):generate.index("case GenFaacSLH:")]
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef unsigned SetType;
''' + without_includes((ROOT / 'applications/main/subghz/helpers/subghz_gen_info.h').read_text()) + self.policy() + r'''
typedef struct{void*txrx;}SubGhz;
static unsigned writes,with_te,without_te;
static bool gen_ok=true,write_ok=true;
static uint32_t saved_gt;
static bool subghz_txrx_gen_data_protocol_and_te(void*t,const char*m,uint32_t f,const char*n,uint64_t k,unsigned b,uint32_t te){(void)t;assert(!strcmp(m,"AM650")&&f==433920000&&n&&k==0x123&&b==24&&te>0);with_te++;return gen_ok;}
static bool subghz_txrx_gen_data_protocol(void*t,const char*m,uint32_t f,const char*n,uint64_t k,unsigned b){(void)t;(void)m;(void)f;(void)n;(void)k;(void)b;without_te++;return gen_ok;}
static void*subghz_txrx_get_fff_data(void*t){return t;}
static bool flipper_format_update_uint32(void*f,const char*k,const uint32_t*v,unsigned n){(void)f;assert(!strcmp(k,"Guard_time")&&n==1);writes++;saved_gt=*v;return write_ok;}
static bool generate(SubGhz*subghz,GenInfo gen_info){bool generated_protocol=false;switch(gen_info.type){
''' + branch + r'''
default:break;}return generated_protocol;}
int main(void){
 SubGhz s={0};GenInfo g={.type=GenData,.mod="AM650",.freq=433920000,.data={.name="Princeton",.key=0x123,.bits=24,.te=400,.gt=72}};
 assert(subghz_manual_band_get(0)&&subghz_manual_band_index(g.freq,g.mod)==0);
 assert(generate(&s,g)&&saved_gt==72&&writes==1&&with_te==1);
 write_ok=false;assert(!generate(&s,g)&&writes==2);write_ok=true;
 gen_ok=false;assert(!generate(&s,g)&&writes==2);gen_ok=true;
 g.data.gt=14;assert(!generate(&s,g)&&writes==2);
 g.data.gt=73;assert(!generate(&s,g)&&writes==2);
 g.data.gt=0;assert(generate(&s,g)&&writes==2);
 g.data.name="Faac TM";g.data.te=999;assert(generate(&s,g)&&writes==2);
 g.data.gt=30;assert(!generate(&s,g)&&writes==2);
 g.data.gt=0;g.data.te=0;assert(generate(&s,g)&&without_te==1);
 return 0;
}
''')

    def test_advanced_editor_preserves_quick_routing_and_protocol_steps(self):
        source = (PLUGIN / "subghz_scene_set_type.c").read_text()
        actual = function(source, "bool subghz_scene_set_type_proceed_advanced(")
        actual += "\n" + function(source, "bool subghz_scene_set_type_on_event(")
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef unsigned SetType;
''' + without_includes((ROOT / 'applications/main/subghz/helpers/subghz_gen_info.h').read_text()) + r'''
enum{SceneManagerEventTypeCustom,SceneManagerEventTypeBack,SetTypeMAX=128};
enum{SubGhzSceneSetType,SubGhzSceneStart,SubGhzSceneSetKey,SubGhzSceneSetSerial,SubGhzSceneSetManualParams};
enum{SubmenuIndexAddManually,SubmenuIndexAddManuallyAdvanced};
typedef struct{unsigned type,event;}SceneManagerEvent;
typedef struct{GenInfo*gen_info;void*scene_manager;}SubGhz;
static unsigned states[5],next_scene,generated,unsupported,type;
static unsigned scene_manager_get_scene_state(void*s,unsigned id){(void)s;return states[id];}
static void scene_manager_set_scene_state(void*s,unsigned id,unsigned value){(void)s;states[id]=value;}
static void scene_manager_next_scene(void*s,unsigned id){(void)s;next_scene=id;}
static bool subghz_scene_set_type_generate_protocol_from_infos(SubGhz*s){(void)s;generated++;return true;}
static void subghz_scene_show_unsupported(SubGhz*s){(void)s;unsupported++;}
void subghz_gen_info_reset(GenInfo*g){memset(g,0,sizeof(*g));}
void subghz_scene_set_type_fill_generation_infos(GenInfo*g,SetType row){(void)row;g->type=type;}
''' + actual + r'''
int main(void){GenInfo g={0};SubGhz s={.gen_info=&g};
 states[SubGhzSceneStart]=SubmenuIndexAddManuallyAdvanced;
 for(type=GenData;type<=GenUnsupported;type++){
  next_scene=99;generated=unsupported=0;
  assert(subghz_scene_set_type_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,10}));
  assert(next_scene==SubGhzSceneSetManualParams&&!generated&&!unsupported&&states[SubGhzSceneSetType]==10);
  assert(subghz_scene_set_type_proceed_advanced(&s));
  if(type==GenData)assert(next_scene==SubGhzSceneSetKey);
  else if(type==GenSecPlus1)assert(generated==1);
  else if(type==GenUnsupported)assert(unsupported==1);
  else assert(next_scene==SubGhzSceneSetSerial);
 }
 states[SubGhzSceneStart]=SubmenuIndexAddManually;generated=0;
 assert(subghz_scene_set_type_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,10}));assert(generated==1);
 assert(!subghz_scene_set_type_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,SetTypeMAX}));
 assert(!subghz_scene_set_type_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeBack,10}));
 return 0;
}
''')

    def test_canceling_key_edit_retains_original_numeric_key(self):
        source = (PLUGIN / "subghz_scene_set_key.c").read_text()
        state = "\n".join(re.findall(r"(?m)^static (?:bool|uint64_t) subghz_set_key_.*;$", source))
        actual = function(source, "void subghz_scene_set_key_on_enter(")
        actual += "\n" + function(source, "bool subghz_scene_set_key_on_event(")
        actual += "\n" + function(source, "void subghz_scene_set_key_on_exit(")
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define furi_assert assert
#define __bswap64 __builtin_bswap64
#define SUBGHZ_MANUAL_PARAMETERS 1
enum{GenData,GenKeeloq,SubGhzViewIdByteInput,SubGhzCustomEventByteInputDone,SubGhzSceneShowError,SubGhzSceneSaveName,SubGhzCustomEventManagerSet};
enum{SceneManagerEventTypeCustom,SceneManagerEventTypeBack};
typedef struct{unsigned type,event;}SceneManagerEvent;
typedef struct{unsigned type;struct{uint64_t key;}data;}GenInfo;
typedef struct{GenInfo*gen_info;void*byte_input,*view_dispatcher,*error_str,*scene_manager;}SubGhz;
typedef void ByteInput;
static uint8_t* writable;
static unsigned generated;
static bool subghz_scene_set_type_generate_protocol_from_infos(SubGhz*s){(void)s;generated++;return true;}
static void furi_string_set(void*s,const char*t){(void)s;(void)t;}
static void scene_manager_next_scene(void*s,unsigned n){(void)s;(void)n;}
static void scene_manager_set_scene_state(void*s,unsigned n,unsigned v){(void)s;(void)n;(void)v;}
static void subghz_file_name_clear(SubGhz*s){(void)s;}
static void subghz_scene_set_key_byte_input_callback(void*c){(void)c;}
static void subghz_scene_show_unsupported(SubGhz*s){(void)s;}
static void byte_input_set_header_text(void*i,const char*t){(void)i;(void)t;}
static void byte_input_set_result_callback(void*i,void(*cb)(void*),void*x,void*c,uint8_t*p,uint8_t n){(void)i;(void)cb;(void)x;(void)c;(void)n;writable=p;}
static void view_dispatcher_switch_to_view(void*v,unsigned id){(void)v;(void)id;}
''' + state + actual + r'''
int main(void){
 const uint64_t original=0x0123456789ABCDEFULL;GenInfo g={.type=GenData,.data={original}};SubGhz s={.gen_info=&g};
 for(unsigned n=0;n<3;n++){
  subghz_scene_set_key_on_enter(&s);assert(writable);writable[0]^=0x80;
  subghz_scene_set_key_on_exit(&s);assert(g.data.key==original&&!writable);
 }
 subghz_scene_set_key_on_enter(&s);writable[0]=0xFE;
 assert(!subghz_scene_set_key_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeBack,0}));
 assert(!subghz_scene_set_key_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,999}));
 assert(subghz_scene_set_key_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,SubGhzCustomEventByteInputDone}));
 assert(generated==1&&g.data.key==0xFE23456789ABCDEFULL);
 subghz_scene_set_key_on_exit(&s);assert(g.data.key==0xFE23456789ABCDEFULL);
 g.type=GenKeeloq;subghz_scene_set_key_on_enter(&s);
 assert(subghz_scene_set_key_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,SubGhzCustomEventByteInputDone}));
 subghz_scene_set_key_on_exit(&s);assert(generated==1);
 return 0;
}
''')

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
typedef struct {View view;void(*callback)(void*,int32_t);void*ctx;int32_t min,max,value;const char*header;}NumberInput;
typedef struct Item {unsigned index,count;const char*label;char text[32];void*ctx;void(*change)(struct Item*);}VariableItem;
typedef struct {VariableItem items[4];unsigned count;void(*enter)(void*,uint32_t);void*ctx;}VariableItemList;
typedef struct {GenInfo*gen_info;VariableItemList*variable_item_list;void*view_dispatcher,*scene_manager,*txrx,*error_str;}SubGhz;
static unsigned current_view,added,removed,freed,last_event,last_scene,proceeded;
static bool alloc_ok=true,preset_ok=true;
static VariableItem* variable_item_list_add(VariableItemList*l,const char*n,uint8_t count,void(*cb)(VariableItem*),void*ctx){assert(l->count<4);VariableItem*i=&l->items[l->count++];i->ctx=ctx;i->change=cb;i->label=n;i->count=count;return i;}
static void variable_item_list_reset(VariableItemList*l){l->count=0;}
static VariableItem* variable_item_list_get(VariableItemList*l,uint8_t index){assert(index<l->count);return &l->items[index];}
static void variable_item_list_set_enter_callback(VariableItemList*l,void(*cb)(void*,uint32_t),void*ctx){l->enter=cb;l->ctx=ctx;}
static void variable_item_set_current_value_index(VariableItem*i,uint8_t n){i->index=n;}
static void __attribute__((unused)) variable_item_set_item_label(VariableItem*i,const char*t){i->label=t;}
static uint8_t variable_item_get_current_value_index(VariableItem*i){return i->index;}
static void* variable_item_get_context(VariableItem*i){return i->ctx;}
static void variable_item_set_current_value_text(VariableItem*i,const char*t){snprintf(i->text,sizeof(i->text),"%s",t);}
static void variable_item_list_set_selected_item(VariableItemList*l,uint8_t i){(void)l;(void)i;}
static NumberInput* number_input_alloc(void){return alloc_ok?calloc(1,sizeof(NumberInput)):NULL;}
static void number_input_free(NumberInput*i){freed++;free(i);}
static View* number_input_get_view(NumberInput*i){return &i->view;}
static void number_input_set_header_text(NumberInput*i,const char*t){i->header=t;}
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
 assert(!strcmp(list.items[0].label,"AM650")&&!strcmp(list.items[0].text,"433.92"));
 list.items[0].index=8;list.items[0].change(&list.items[0]);
 assert(!strcmp(list.items[0].label,"FM476")&&!strcmp(list.items[0].text,"868.80"));
 list.items[0].index=12;list.items[0].change(&list.items[0]);assert(g.freq==315000000&&!strcmp(g.mod,"AM650"));
 list.items[0].index=255;list.items[0].change(&list.items[0]);assert(g.freq==315000000);
 list.enter(&s,1);assert(added==1&&current_view==SubGhzViewIdNumberInput);
 assert(subghz_manual_number_back(&s)==SubGhzViewIdVariableItemList);
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
 g.type=GenKeeloq;subghz_scene_set_manual_params_on_enter(&s);list.enter(&s,1);assert(!manual_input);
 subghz_scene_set_manual_params_on_exit(&s);g.type=GenData;
 g.data.name="Faac TM";subghz_scene_set_manual_params_on_enter(&s);list.enter(&s,2);assert(!manual_input);
 subghz_scene_set_manual_params_on_exit(&s);g.data.name="Princeton";
 alloc_ok=false;subghz_scene_set_manual_params_on_enter(&s);list.enter(&s,1);assert(!manual_input&&last_scene==SubGhzSceneShowError);
 subghz_scene_set_manual_params_on_exit(&s);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
