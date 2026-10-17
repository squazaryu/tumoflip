"""Synthetic native regressions for Nice 72-bit files and GDR view lifetime."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
GDR = ROOT / "applications_user/garage_door_remote"


class Nice72SettingsTests(unittest.TestCase):
    def test_nice_one_generator_is_available_without_replacing_52_bit(self):
        source = (ROOT / "lib/subghz/protocols/nice_flor_s.c").read_text()
        self.assertIn("bool subghz_protocol_nice_o_one_create_data(", source)
        self.assertIn("bool subghz_protocol_nice_o_create_data(", source)
        menu = (ROOT / "applications/main/subghz/plugins/add_manually/subghz_gen_info.c").read_text()
        self.assertIn("case SetTypeNiceOCodeOne_433_92:", menu)

    def test_capture_export_keeps_the_received_shape(self):
        source = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        self.assertIn("key_bits", source)
        self.assertIn("subghz_protocol_nice_o_one_create_data(", source)
        self.assertNotIn("uint32_t bits = 52;", function(source, "static bool nice_o_save("))

    def test_configuration_view_detaches_before_free_and_is_idempotent(self):
        source = (GDR / "scenes/protopirate_scene_receiver_config.c").read_text()
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


if __name__ == "__main__":
    unittest.main()
