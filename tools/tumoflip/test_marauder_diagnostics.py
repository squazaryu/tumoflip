"""Native menu bounds and exact stop command tests; no UART or radio access."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/esp32_wifi_marauder"


class MarauderDiagnosticsTest(unittest.TestCase):
    def test_poi_long_press_preserves_scrolling_back_and_other_commands(self):
        code = (APP / "wifi_marauder_console_policy.h").read_text().replace("#pragma once", "")
        handler = function((APP / "wifi_marauder_app.c").read_text(), "static bool wifi_marauder_console_input(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
enum{InputKeyOk,InputKeyBack,InputKeyUp,InputTypeLong,InputTypeShort,WifiMarauderEventMarkPoi};
typedef struct{int key,type;}InputEvent;
typedef struct{bool is_command;void*script;const char*selected_tx_string;void*view_dispatcher;}WifiMarauderApp;
static int marks;
static void view_dispatcher_send_custom_event(void*d,int e){(void)d;assert(e==WifiMarauderEventMarkPoi);marks++;}
''' + code + handler + r'''
int main(void){WifiMarauderApp app={true,NULL,"wardrive",NULL};InputEvent e={InputKeyOk,InputTypeLong};
 assert(wifi_marauder_console_stop_command("recon status")==NULL);
 assert(wifi_marauder_console_input(&e,&app)&&marks==1);
 e.type=InputTypeShort;assert(wifi_marauder_console_input(&e,&app)&&marks==1);
 e.key=InputKeyUp;assert(!wifi_marauder_console_input(&e,&app));
 e.key=InputKeyBack;assert(!wifi_marauder_console_input(&e,&app));
 e.key=InputKeyOk;e.type=InputTypeLong;app.selected_tx_string="wardrivepoi";assert(!wifi_marauder_console_input(&e,&app));
 app.selected_tx_string="recon wifi";assert(!wifi_marauder_console_input(&e,&app));
 app.selected_tx_string="wardrive";app.script=&app;assert(!wifi_marauder_console_input(&e,&app));assert(marks==1);return 0;}
''')

    def test_real_menu_rows_have_bounded_commands_and_keep_special_tail(self):
        source = (APP / "scenes/wifi_marauder_scene_start.c").read_text()
        table = source[source.index("#define MAX_OPTIONS"):source.index("_Static_assert")]
        count = re.search(r"#define NUM_MENU_ITEMS \((\d+)\)",
                          (APP / "wifi_marauder_app_i.h").read_text())[1]
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef enum{NO_ARGS,INPUT_ARGS,TOGGLE_ARGS}InputArgs;
typedef enum{FOCUS_CONSOLE_START,FOCUS_CONSOLE_END,FOCUS_CONSOLE_TOGGLE}FocusConsole;
enum{NO_TIP,SHOW_STOPSCAN_TIP};
''' + table + '\n#define NUM_MENU_ITEMS ' + count + r'''
int main(void){
 assert(sizeof(items)/sizeof(items[0])==NUM_MENU_ITEMS);
 bool recon=false,protocol=false;
 for(unsigned i=0;i<NUM_MENU_ITEMS;i++){
   const WifiMarauderItem*row=&items[i];assert(row->item_string&&row->num_options_menu>0&&row->num_options_menu<=MAX_OPTIONS);
   for(int j=0;j<row->num_options_menu;j++){assert(row->options_menu[j]&&row->actual_commands[j]);assert(!strchr(row->actual_commands[j],'\n'));assert(strlen(row->actual_commands[j])<512);}
   if(!strcmp(row->item_string,"Recon")){recon=true;assert(!strcmp(row->actual_commands[3],"recon stop"));}
   if(!strcmp(row->item_string,"Protocol Info")){protocol=true;assert(!strcmp(row->actual_commands[0],"protocolinfo"));}
 }
 assert(recon&&protocol);assert(!strcmp(items[NUM_MENU_ITEMS-2].item_string,"Scripts"));
 assert(!strcmp(items[NUM_MENU_ITEMS-1].item_string,"Save to flipper sdcard"));return 0;}
''')

    def test_back_stops_recon_task_and_preserves_existing_scan_stop(self):
        code = (APP / "wifi_marauder_console_policy.h").read_text().replace("#pragma once", "")
        native('#include <assert.h>\n' + code + r'''
int main(void){
 assert(wifi_marauder_console_can_mark_poi("wardrive"));
 assert(!wifi_marauder_console_can_mark_poi(NULL));
 assert(wifi_marauder_console_stop_command("protocolinfo")==NULL);
 assert(wifi_marauder_console_stop_command("backupspiffs")==NULL);
 assert(!strcmp(wifi_marauder_console_stop_command("recon wifi"),"recon stop\n"));
 assert(!strcmp(wifi_marauder_console_stop_command("recon ble"),"recon stop\n"));
 assert(!strcmp(wifi_marauder_console_stop_command("nmea"),"stopscan\n"));
 assert(!strcmp(wifi_marauder_console_stop_command("scanall"),"stopscan\n"));
 assert(!strcmp(wifi_marauder_console_stop_command(NULL),"stopscan\n"));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
