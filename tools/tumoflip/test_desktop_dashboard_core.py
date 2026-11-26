"""Actual core glue on synthetic views/timers/profile reads; no app/device launch."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "applications/services/desktop/desktop_panel_i.h"
#include "furi/core/system_policy_i.h"
#define furi_assert assert
typedef void View;
enum {DesktopSceneMain,DesktopSceneLocked,DesktopSceneActivity,DesktopSceneQuickSettings};
typedef struct {
 DesktopSettings settings;DesktopPanelSettings panel;bool dashboard_mounted,locked,app_running;
 void* animation_manager,*main_view_stack,*dashboard,*dashboard_timer;
 uint32_t dashboard_period;
} Desktop;
static DesktopPanelSettings disk;
static bool readable;
static void* mounted=(void*)0x10;
static unsigned swaps,signals,updates,starts,stops;
static bool decoration_quiet,last_alive,last_animate;
static uint32_t tick;
static inline bool desktop_panel_load(DesktopPanelSettings*p){if(readable)*p=disk;return readable;}
static inline void*animation_manager_get_animation_view(void*p){assert(p);return(void*)0x10;}
static inline void*desktop_activity_get_view(void*p){assert(p);return(void*)0x20;}
static inline void view_stack_remove_view(void*s,void*v){assert(s&&v==mounted);mounted=NULL;swaps++;}
static inline void view_stack_add_view(void*s,void*v){assert(s&&!mounted);mounted=v;}
static inline void animation_manager_set_quiet(void*p,bool quiet){assert(p);decoration_quiet=quiet;}
static inline void desktop_policy_changed(void*p){assert(p);signals++;}
static inline void desktop_activity_update(void*p,const TumoSystemState*s,unsigned holds){assert(p&&s&&holds==7);updates++;}
static inline unsigned furi_hal_power_insomnia_level(void){return 7;}
static inline unsigned furi_get_tick(void){return tick;}
static inline unsigned furi_ms_to_ticks(unsigned n){return n;}
static inline unsigned desktop_activity_home_tick(void*p,unsigned n,bool alive,bool animate){assert(p&&n==tick);last_alive=alive;last_animate=animate;return alive&&animate?250:0;}
static inline void furi_timer_start(void*p,unsigned n){assert(p&&n==250);starts++;}
static inline void furi_timer_stop(void*p){assert(p);stops++;}
FavoriteApp* desktop_favorite(Desktop*,FavoriteAppShortcut);
static void desktop_reload_panel(Desktop*);
static void desktop_dashboard_mount(Desktop*);
static void desktop_dashboard_refresh(Desktop*,const TumoSystemState*,uint32_t);
'''

MAIN = r'''
int main(void){
 Desktop d={.animation_manager=(void*)1,.main_view_stack=(void*)2,.dashboard=(void*)3,.dashboard_timer=(void*)4};
 for(unsigned i=0;i<FavoriteAppNumber;i++)strcpy(d.settings.favorite_apps[i].name_or_path,"Wallpaper app");
 DesktopSettings before=d.settings;desktop_panel_defaults(&d.panel,&d.settings);
 assert(desktop_favorite(&d,FavoriteAppLeftShort)==&d.settings.favorite_apps[FavoriteAppLeftShort]);
 assert(!desktop_favorite(&d,(FavoriteAppShortcut)-1)&&!desktop_favorite(&d,FavoriteAppNumber));
 desktop_reload_panel(&d);assert(!swaps&&!d.dashboard_mounted);
 disk=d.panel;disk.mode=DesktopModeDashboard;strcpy(disk.held[0].name_or_path,"Panel app");readable=true;
 d.locked=true;desktop_reload_panel(&d);assert(!swaps&&!d.dashboard_mounted);
 TumoSystemState state={.display_on=true};
 desktop_dashboard_refresh(&d,&state,DesktopSceneLocked);assert(!swaps);
 d.locked=false;d.app_running=true;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(!swaps);
 d.app_running=false;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);
 assert(swaps==1&&mounted==(void*)0x20&&decoration_quiet&&d.dashboard_mounted);
 assert(!desktop_favorite(&d,FavoriteAppLeftShort)&&!desktop_favorite(&d,FavoriteAppRightShort));
 assert(!strcmp(desktop_favorite(&d,FavoriteAppLeftLong)->name_or_path,"Panel app"));
 assert(!memcmp(&d.settings,&before,sizeof(before)));
 desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(last_alive&&last_animate&&starts==1&&updates==2);
 desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(starts==1);
 d.locked=true;desktop_dashboard_refresh(&d,&state,DesktopSceneLocked);assert(!last_alive&&stops==1);
 d.locked=false;d.app_running=true;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(!last_alive);
 d.app_running=false;d.panel.motion=0;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(last_alive&&!last_animate);
 d.panel.motion=1;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(starts==2);
 state.display_on=false;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(!last_animate&&stops==2);
 state.display_on=true;state.recovery=true;desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(!last_animate);
 disk.mode=DesktopModeWallpaper;desktop_reload_panel(&d);assert(swaps==1);
 desktop_dashboard_refresh(&d,&state,DesktopSceneMain);assert(swaps==2&&mounted==(void*)0x10);
 assert(desktop_favorite(&d,FavoriteAppLeftLong)==&d.settings.favorite_apps[FavoriteAppLeftLong]);
 readable=false;desktop_reload_panel(&d);assert(swaps==2&&!memcmp(&d.settings,&before,sizeof(before)));
 assert(signals>=3);
 return 0;
}
'''


class DesktopDashboardCoreTests(unittest.TestCase):
    def test_profile_routing_stack_swap_pin_and_timer_lifecycle(self):
        source = (ROOT / "applications/services/desktop/desktop.c").read_text()
        functions = []
        for name in ("desktop_favorite", "desktop_dashboard_mount", "desktop_reload_panel", "desktop_dashboard_refresh"):
            match = re.search(r"(?:static )?(?:void|FavoriteApp\*)\s+"+name+r"\([^;]+?\)\s*\{", source)
            if match:
                start = source.index("{", match.start()); end = start + 1; depth = 1
                while depth:
                    depth += (source[end] == "{") - (source[end] == "}"); end += 1
                functions.append(source[match.start():end])
        with tempfile.TemporaryDirectory(prefix="dashboard-core-") as directory:
            work = Path(directory)
            (work / "test.c").write_text(HARNESS + "\n".join(functions) + MAIN)
            compiled = subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-function", "-Werror",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", str(ROOT),
                str(work / "test.c"), "-o", str(work / "test")], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(work / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
