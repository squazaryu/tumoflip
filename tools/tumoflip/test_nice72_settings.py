"""Synthetic native regressions for Nice 72-bit files and GDR view lifetime."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
GDR = ROOT / "applications_user/garage_door_remote"


class Nice72SettingsTests(unittest.TestCase):
    def test_native_52_and_72_generators_validate_and_serialize_tail(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        signatures = (
            "static uint32_t subghz_protocol_nice_one_crc(",
            "static void subghz_protocol_nice_one_get_data(uint8_t* p, uint8_t num_parcel, uint8_t hold_bit) {",
            "static bool subghz_protocol_nice_o_make_data(",
            "static bool subghz_protocol_nice_o_create_data_bits(",
            "bool subghz_protocol_nice_o_create_data(",
            "bool subghz_protocol_nice_o_one_create_data(",
        )
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
#define NICE_ONE_COUNT_BIT 72
#define NICE_FLOR_S_IC_PLAIN 0xffffu
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE UINT64_MAX
typedef struct {uint64_t data,data_2;uint32_t serial,cnt;uint16_t data_count_bit;} SubGhzBlockGeneric;
typedef struct {SubGhzBlockGeneric generic;uint16_t ic;bool o_code;const char* nice_flor_s_rainbow_table_file_name;} SubGhzProtocolEncoderNiceFlorS;
typedef struct {uint64_t data;uint32_t tail;uint16_t bits,ic;bool fail,fail_ic;} FlipperFormat;
typedef int SubGhzRadioPreset;
enum {SubGhzProtocolStatusOk,SubGhzProtocolStatusErrorParserOthers};
typedef int SubGhzProtocolStatus;
bool subghz_protocol_blocks_get_bit_array(uint8_t* p,unsigned bit){return (p[bit/8]>>(7-bit%8))&1;}
void subghz_protocol_nice_o_mask(uint8_t p,uint32_t* m,uint16_t* n){assert(p==3);*m=0;*n=0;}
uint64_t subghz_protocol_nice_flor_s_encrypt_ic(uint64_t plain,uint16_t ic,const char* path){(void)ic;return path?plain:UINT64_MAX;}
SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric* g,FlipperFormat* f,SubGhzRadioPreset* p){(void)p;if(f->fail)return 1;f->data=g->data;f->bits=g->data_count_bit;f->tail=(g->data_2>>4)&0xfffff;return 0;}
bool flipper_format_rewind(FlipperFormat* f){return !f->fail;}
bool flipper_format_insert_or_update_hex(FlipperFormat* f,const char* k,uint8_t* v,size_t n){(void)k;assert(n==2);f->ic=(uint16_t)v[0]<<8|v[1];return !f->fail_ic;}
''' + "\n".join(function(source, s) for s in signatures) + r'''
int main(void){
 SubGhzProtocolEncoderNiceFlorS e={.nice_flor_s_rainbow_table_file_name="synthetic"};
 FlipperFormat f={0};SubGhzRadioPreset p=0;
 assert(subghz_protocol_nice_o_create_data(&e,&f,0xfffffff,15,65535,0,&p));
 assert(f.bits==52 && !f.tail && f.ic==0);
 assert(subghz_protocol_nice_o_one_create_data(&e,&f,0xfffffff,15,65535,0,&p));
 assert(f.bits==72 && f.tail && f.tail<=0xfffff);
 uint8_t bytes[10]={0};for(unsigned i=0;i<7;i++)bytes[i]=f.data>>(48-i*8);
 bytes[7]=f.tail>>12;bytes[8]=f.tail>>4;bytes[9]=f.tail<<4;
 unsigned crc=subghz_protocol_nice_one_crc(bytes);
 assert(((bytes[8]&15)<<4|(bytes[9]>>4))==crc);
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,0x10000000,1,0,1,&p));
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,1,16,0,1,&p));
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,1,1,0,65535,&p));
 e.nice_flor_s_rainbow_table_file_name=NULL;
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,1,1,0,1,&p));
 e.nice_flor_s_rainbow_table_file_name="synthetic";f.fail=true;
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,1,1,0,1,&p));
 f.fail=false;f.fail_ic=true;
 assert(!subghz_protocol_nice_o_one_create_data(&e,&f,1,1,0,1,&p));
 return 0;
}
''')

    def test_native_settings_plugin_failures_do_not_save_defaults(self):
        for app in (GDR, ROOT / "applications_user/protopirate"):
            self.check_settings_host(app)

    def check_settings_host(self, app):
        source = (app / "helpers/protopirate_settings_host.c").read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
#define APP_ASSETS_PATH(x) x
#define GDR_SETTINGS_PLUGIN_ID "gdr_settings"
#define GDR_SETTINGS_PLUGIN_VERSION 1
#define PP_SETTINGS_PLUGIN_ID "pp_settings"
#define PP_SETTINGS_PLUGIN_VERSION 1
typedef struct {uint32_t frequency,dual_freq_a,dual_freq_b,shield_freq;uint8_t dual_preset_a,dual_preset_b,shield_tx_offset_index;} ProtoPirateSettings;
typedef struct {size_t settings_size;void (*load)(ProtoPirateSettings*);void (*save)(ProtoPirateSettings*,...);} ProtoPirateSettingsPlugin;
typedef int PluginManager;
static bool settings_loaded;
static int mode, frees, loads, saves, live;
enum {PluginManagerErrorNone};
static void* firmware_api_interface;
void load(ProtoPirateSettings* s){s->frequency=315000000;loads++;}
void save(ProtoPirateSettings* s,...){assert(s->frequency==315000000);saves++;}
static void* __attribute__((unused)) protopirate_storage_commit_temp_file;
PluginManager* plugin_manager_alloc(const char* id,int v,void* api){(void)id;(void)v;(void)api;if(mode==1)return NULL;assert(!live);live=1;return &live;}
int plugin_manager_load_single(PluginManager* m,const char* path){(void)m;(void)path;return mode==2?1:0;}
void plugin_manager_free(PluginManager* m){assert(m && live);live=0;frees++;}
const void* plugin_manager_get_ep(PluginManager* m,int i){(void)m;(void)i;static ProtoPirateSettingsPlugin p;p=(ProtoPirateSettingsPlugin){sizeof(ProtoPirateSettings),load,save};if(mode==3)return NULL;if(mode==4)p.settings_size++;if(mode==5)p.load=NULL;return &p;}
''' + "\n".join(function(source, s) for s in (
            "void protopirate_settings_set_defaults(",
            "static bool protopirate_settings_call(",
            "void protopirate_settings_load(",
            "void protopirate_settings_save(",
        )) + r'''
int main(void){
 ProtoPirateSettings s;
 for(mode=1;mode<=5;mode++){
  protopirate_settings_load(&s);assert(s.frequency==433920000 && !live);
  protopirate_settings_save(&s);assert(!saves && !loads && !live);
 }
 mode=0;protopirate_settings_load(&s);assert(loads==1 && s.frequency==315000000 && !live);
 protopirate_settings_save(&s);assert(saves==1 && !live);
 mode=2;protopirate_settings_save(&s);assert(saves==1 && !live && frees==7);
 return 0;
}
''')

    def test_nice_one_generator_is_available_without_replacing_52_bit(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        self.assertIn("bool subghz_protocol_nice_o_one_create_data(", source)
        self.assertIn("bool subghz_protocol_nice_o_create_data(", source)
        menu = (ROOT / "applications/main/subghz/plugins/add_manually/subghz_gen_info.c").read_text()
        self.assertIn("case SetTypeNiceOCodeOne_433_92:", menu)

    def test_capture_export_keeps_the_received_shape(self):
        source = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        self.assertIn("key_bits", source)
        self.assertIn("subghz_protocol_nice_o_one_create_data", source)
        self.assertNotIn("uint32_t bits = 52;", function(source, "static bool nice_o_save("))

    def test_configuration_view_detaches_before_free_and_is_idempotent(self):
        for app in (GDR, ROOT / "applications_user/protopirate"):
            self.check_configuration_view(app)

    def check_configuration_view(self, app):
        source = (app / "scenes/protopirate_scene_receiver_config.c").read_text()
        native(r'''
#include <assert.h>
#include <stddef.h>
typedef struct {void* variable_item_list;void* view_dispatcher;} ProtoPirateApp;
enum {ProtoPirateViewSubmenu,ProtoPirateViewVariableItemList};
static unsigned step, frees;
void variable_item_list_set_selected_item(void* p,int n){(void)p;(void)n;}
void variable_item_list_reset(void* p){(void)p;}
void view_dispatcher_switch_to_view(void* d,int id){(void)d;assert(id==ProtoPirateViewSubmenu);assert(step++==0);}
void view_dispatcher_remove_view(void* d,int id){(void)d;assert(id==ProtoPirateViewVariableItemList);assert(step++==1);}
void variable_item_list_free(void* p){assert(p);assert(step++==2);frees++;}
''' + function(source, "void protopirate_scene_receiver_config_on_exit(") + r'''
int main(void){
 ProtoPirateApp app={.variable_item_list=&app};
 protopirate_scene_receiver_config_on_exit(&app);
 assert(!app.variable_item_list && frees==1 && step==3);
 protopirate_scene_receiver_config_on_exit(&app);
 assert(frees==1);return 0;
}
''')

    def test_settings_are_loaded_only_for_the_operation(self):
        manifest = (GDR / "application.fam").read_text()
        self.assertIn('appid="garage_door_remote_settings_plugin"', manifest)
        host = GDR / "helpers/protopirate_settings_host.c"
        self.assertTrue(host.exists(), "settings codec must leave the resident FAP")

    def test_rx_only_protopirate_gets_the_same_lifetime_adaptation(self):
        app = ROOT / "applications_user/protopirate"
        manifest = (app / "application.fam").read_text()
        self.assertIn('appid="protopirate_settings_plugin"', manifest)
        self.assertTrue((app / "helpers/protopirate_settings_host.c").exists())
        scene = (app / "scenes/protopirate_scene_receiver_config.c").read_text()
        exit_scene = function(scene, "void protopirate_scene_receiver_config_on_exit(")
        self.assertIn("view_dispatcher_remove_view", exit_scene)
        self.assertIn("app->variable_item_list = NULL", exit_scene)


if __name__ == "__main__":
    unittest.main()
