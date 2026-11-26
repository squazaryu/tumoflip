"""Execute actual Settings callbacks without a GUI/device or filesystem writes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/settings/desktop_settings"

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "applications/services/desktop/desktop_panel_i.h"
#define DESKTOP_PANEL_REFRESH 1000
typedef struct {DesktopSettings settings;DesktopPanelSettings panel;DesktopMode shortcut_profile;
 bool panel_dirty,supports_panel;uint8_t menu_idx;void*view_dispatcher;} DesktopSettingsApp;
typedef struct {unsigned index;DesktopSettingsApp*app;char text[24];} VariableItem;
static unsigned event,writes;static bool confirmed;
static inline void*variable_item_get_context(VariableItem*i){return i->app;}
static inline unsigned variable_item_get_current_value_index(VariableItem*i){return i->index;}
static inline void variable_item_set_current_value_text(VariableItem*i,const char*s){assert(strlen(s)<sizeof(i->text));strcpy(i->text,s);}
static inline void view_dispatcher_send_custom_event(void*p,unsigned e){assert(p);event=e;}
static inline bool desktop_panel_store(DesktopPanelSettings*p){assert(p);writes++;return confirmed;}
FavoriteApp* desktop_settings_favorite(DesktopSettingsApp*,FavoriteAppShortcut);
bool desktop_settings_store_panel(DesktopSettingsApp*);
uint32_t desktop_settings_entry_for_index(DesktopSettingsApp*,uint32_t);
static void desktop_settings_scene_start_mode_changed(VariableItem*);
static void desktop_settings_scene_start_shortcut_set_changed(VariableItem*);
static void desktop_settings_scene_start_panel_motion_changed(VariableItem*);
'''

MAIN = r'''
int main(void){
 DesktopSettingsApp app={.view_dispatcher=(void*)1,.supports_panel=true};
 for(unsigned i=0;i<FavoriteAppNumber;i++)strcpy(app.settings.favorite_apps[i].name_or_path,"Wallpaper original");
 DesktopSettings before=app.settings;desktop_panel_defaults(&app.panel,&app.settings);
 VariableItem item={.index=1,.app=&app};desktop_settings_scene_start_mode_changed(&item);
 assert(app.panel.mode==DesktopModeDashboard&&app.shortcut_profile==DesktopModeDashboard&&app.panel_dirty);
 assert(!strcmp(item.text,"Panel")&&event==DESKTOP_PANEL_REFRESH&&!writes);
 assert(!desktop_settings_favorite(&app,FavoriteAppLeftShort));
 strcpy(desktop_settings_favorite(&app,FavoriteAppLeftLong)->name_or_path,"Dashboard left");
 assert(!memcmp(&app.settings,&before,sizeof(before)));
 app.panel_dirty=false;item.index=0;desktop_settings_scene_start_shortcut_set_changed(&item);
 assert(app.panel.mode==DesktopModeDashboard&&app.shortcut_profile==DesktopModeWallpaper&&!app.panel_dirty);
 assert(desktop_settings_favorite(&app,FavoriteAppLeftLong)==&app.settings.favorite_apps[FavoriteAppLeftLong]);
 item.index=0;desktop_settings_scene_start_panel_motion_changed(&item);
 assert(!app.panel.motion&&app.panel_dirty&&!strcmp(item.text,"Off")&&!writes);
 item.index=2;desktop_settings_scene_start_mode_changed(&item);assert(app.panel.mode==DesktopModeDashboard);
 assert(!desktop_settings_store_panel(&app)&&app.panel_dirty&&writes==1);
 confirmed=true;assert(desktop_settings_store_panel(&app)&&!app.panel_dirty&&writes==2);
 assert(desktop_settings_store_panel(&app)&&writes==2); // Viewing Settings does not rewrite the sidecar.
 assert(!desktop_settings_favorite(&app,(FavoriteAppShortcut)-1));
 app.supports_panel=false;app.shortcut_profile=DesktopModeDashboard;app.panel_dirty=false;item.index=0;
 desktop_settings_scene_start_mode_changed(&item);assert(app.panel.mode==DesktopModeDashboard&&!app.panel_dirty);
 assert(desktop_settings_favorite(&app,FavoriteAppLeftLong)==&app.settings.favorite_apps[FavoriteAppLeftLong]);
 assert(desktop_settings_entry_for_index(&app,0)==DesktopSettingsPinSetup);
 assert(desktop_settings_entry_for_index(&app,1)==DesktopSettingsAutoLockDelay);
 app.supports_panel=true;assert(desktop_settings_entry_for_index(&app,1)==DesktopSettingsPanelMode);
 return 0;
}
'''


def optional_function(source, name):
    match = re.search(r"(?:static )?(?:void|bool|uint32_t|FavoriteApp\*)\s+"+name+r"\([^;]+?\)\s*\{", source)
    if not match:
        return ""
    start = source.index("{", match.start()); end = start + 1; depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}"); end += 1
    return source[match.start():end]


class DesktopPanelSettingsTests(unittest.TestCase):
    def test_production_list_builder_for_both_profiles_and_older_core(self):
        from tools.tumoflip.render_desktop_panel_settings import render
        with tempfile.TemporaryDirectory(prefix="panel-settings-frames-") as directory:
            output=Path(directory);render(output)
            self.assertEqual(len(list(output.glob("*.pgm"))),6)

    def test_mode_editor_scope_save_result_and_wallpaper_preservation(self):
        start = (APP / "scenes/desktop_settings_scene_start.c").read_text()
        app = (APP / "desktop_settings_app.c").read_text()
        enums = re.search(r"typedef enum \{.*?\} DesktopSettingsEntry;", start, re.S).group()
        strings = "\n".join(re.findall(r"static const char\* const desktop_\w+_text\[\]\s*=\s*\{.*?\};", start, re.S))
        functions = "\n".join(optional_function(start, name) for name in (
            "desktop_settings_scene_start_mode_changed", "desktop_settings_scene_start_shortcut_set_changed",
            "desktop_settings_scene_start_panel_motion_changed", "desktop_settings_entry_for_index"))
        functions += optional_function(app, "desktop_settings_favorite") + optional_function(app, "desktop_settings_store_panel")
        with tempfile.TemporaryDirectory(prefix="dashboard-settings-") as directory:
            work = Path(directory)
            (work / "test.c").write_text(HARNESS + enums + strings + functions + MAIN)
            compiled = subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", str(ROOT),
                str(work / "test.c"), "-o", str(work / "test")], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(work / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
