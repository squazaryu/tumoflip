"""Native Settings handlers: capability fallback, profiles, errors and range guards."""
from pathlib import Path
import unittest
from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.render_weather_native import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/settings/desktop_settings/desktop_settings_app.c"
SCENE = ROOT / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c"


def production():
    text = APP.read_text() + "\n" + SCENE.read_text()
    body = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "PANEL_HEADER"
typedef struct {
 DesktopSettings settings;DesktopPanelSettings panel;
 DesktopMode shortcut_profile;bool panel_dirty,supports_panel;uint8_t menu_idx;void*view_dispatcher;
} DesktopSettingsApp;
typedef struct {DesktopSettingsApp*context;uint8_t index;const char*text;} VariableItem;
static unsigned stores,events;static bool save_ok;
bool desktop_panel_store(DesktopPanelSettings*p){assert(p);stores++;return save_ok;}
DesktopSettingsApp* variable_item_get_context(VariableItem*i){return i->context;}
uint8_t variable_item_get_current_value_index(VariableItem*i){return i->index;}
void variable_item_set_current_value_text(VariableItem*i,const char*s){i->text=s;}
void view_dispatcher_send_custom_event(void*d,unsigned event){(void)d;assert(event==1000);events++;}
#define DESKTOP_PANEL_REFRESH 1000
'''.replace("PANEL_HEADER", str(ROOT / "applications/services/desktop/desktop_panel_i.h"))
    enum = SCENE.read_text()
    body += enum[enum.index("typedef enum {"):enum.index("} DesktopSettingsEntry;") + len("} DesktopSettingsEntry;")]
    for declaration in ("static const char* const desktop_mode_text[]", "static const char* const desktop_motion_text[]"):
        if declaration in text:
            start = text.index(declaration)
            body += text[start:text.index(";", start) + 1]
    targets = {
        "FavoriteApp* desktop_settings_favorite(": "FavoriteApp* desktop_settings_favorite(DesktopSettingsApp*,FavoriteAppShortcut);",
        "bool desktop_settings_store_panel(": "bool desktop_settings_store_panel(DesktopSettingsApp*);",
        "uint32_t desktop_settings_entry_for_index(": "uint32_t desktop_settings_entry_for_index(DesktopSettingsApp*,uint32_t);",
        "static void desktop_settings_scene_start_mode_changed(": "void desktop_settings_scene_start_mode_changed(VariableItem*);",
        "static void desktop_settings_scene_start_shortcut_set_changed(": "void desktop_settings_scene_start_shortcut_set_changed(VariableItem*);",
        "static void desktop_settings_scene_start_panel_motion_changed(": "void desktop_settings_scene_start_panel_motion_changed(VariableItem*);",
    }
    for signature, missing in targets.items():
        body += "\n" + (function(text, signature).replace("static void", "void", 1) if signature in text else missing)
    return body


class PanelSettingsTests(unittest.TestCase):
    def test_capability_profiles_and_old_menu_mapping(self):
        native(production() + r'''
int main(void){
 DesktopSettingsApp app={0};
 for(unsigned i=0;i<FavoriteAppNumber;i++)app.settings.favorite_apps[i].name_or_path[0]='C';
 desktop_panel_defaults(&app.panel,&app.settings);app.shortcut_profile=DesktopModeDashboard;
 assert(desktop_settings_favorite(&app,FavoriteAppLeftLong)==&app.settings.favorite_apps[FavoriteAppLeftLong]);
 for(unsigned i=0;i<14;i++)assert(desktop_settings_entry_for_index(&app,i)==(i?i+3:i));
 app.supports_panel=true;
 for(unsigned i=0;i<17;i++)assert(desktop_settings_entry_for_index(&app,i)==i);
 assert(desktop_settings_favorite(&app,FavoriteAppLeftLong)==&app.panel.held[0]);
 assert(!desktop_settings_favorite(&app,FavoriteAppLeftShort));
 assert(!desktop_settings_favorite(&app,FavoriteAppNumber));
 app.panel.held[0].name_or_path[0]='P';
 assert(app.settings.favorite_apps[FavoriteAppLeftLong].name_or_path[0]=='C');
 app.shortcut_profile=DesktopModeWallpaper;
 assert(desktop_settings_favorite(&app,FavoriteAppLeftShort)==&app.settings.favorite_apps[FavoriteAppLeftShort]);
 return 0;
}
''')

    def test_unconfirmed_save_keeps_dirty_and_allows_retry(self):
        native(production() + r'''
int main(void){
 DesktopSettingsApp app={0};
 assert(desktop_settings_store_panel(&app)&&!stores);
 app.panel_dirty=true;assert(!desktop_settings_store_panel(&app)&&app.panel_dirty&&stores==1);
 save_ok=true;assert(desktop_settings_store_panel(&app)&&!app.panel_dirty&&stores==2);
 assert(desktop_settings_store_panel(&app)&&stores==2);return 0;
}
''')

    def test_mode_motion_and_edit_profile_validation(self):
        native(production() + r'''
int main(void){
 DesktopSettingsApp app={0};VariableItem item={.context=&app,.index=1};
 desktop_settings_scene_start_mode_changed(&item);assert(!app.panel_dirty&&!events);
 app.supports_panel=true;item.index=2;
 desktop_settings_scene_start_mode_changed(&item);
 desktop_settings_scene_start_shortcut_set_changed(&item);
 desktop_settings_scene_start_panel_motion_changed(&item);assert(!app.panel_dirty&&!events);
 item.index=1;desktop_settings_scene_start_mode_changed(&item);
 assert(app.panel_dirty&&app.panel.mode==1&&app.shortcut_profile==1&&events==1);
 assert(!strcmp(item.text,"Panel"));app.panel_dirty=false;
 desktop_settings_scene_start_mode_changed(&item);assert(!app.panel_dirty&&events==2);
 item.index=0;desktop_settings_scene_start_shortcut_set_changed(&item);
 assert(app.shortcut_profile==0&&app.panel.mode==1&&!app.panel_dirty&&events==3);
 item.index=1;desktop_settings_scene_start_panel_motion_changed(&item);
 assert(app.panel.motion==1&&app.panel_dirty&&!strcmp(item.text,"On"));
 app.panel_dirty=false;desktop_settings_scene_start_panel_motion_changed(&item);
 assert(!app.panel_dirty);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
