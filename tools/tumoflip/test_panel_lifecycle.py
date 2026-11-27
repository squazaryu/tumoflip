"""Run local panel lifecycle decisions on real C bodies and strict host doubles."""
from pathlib import Path
import unittest
from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'applications/services/desktop/desktop_panel.c'
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "applications/services/desktop/desktop_panel_i.h"
typedef int View;
typedef int FuriTimer;
typedef struct {bool full,window;} Gui;
typedef struct {unsigned scene;} SceneManager;
typedef struct {View* background;} ViewStack;
typedef struct {bool stalled;View view;} AnimationManager;
typedef struct {bool active,motion;uint8_t page;int16_t offset;int8_t direction;} PanelModel;
typedef struct {
 DesktopPanelSettings panel_settings; DesktopSettings settings;
 Gui* gui;SceneManager* scene_manager;ViewStack* main_view_stack;ViewStack* locked_view_stack;
 AnimationManager* animation_manager;View panel_view;PanelModel panel_model;
 FuriTimer* panel_motion_timer;bool panel_attached,panel_stalled,panel_safe_boot,panel_ready;
 bool panel_timer_armed,app_running,locked,panel_awake;
} Desktop;
enum {GuiLayerFullscreen,GuiLayerWindow,DesktopSceneMain=5,DesktopSceneLocked,DesktopScenePinInput};
#define furi_check assert
#define furi_assert assert
static unsigned depth,freezes,thaws,swaps,refreshes,stops,flushes;
static ViewStack* last_added_stack;
#define FURI_CRITICAL_ENTER() (++depth)
#define FURI_CRITICAL_EXIT() do {assert(depth);--depth;} while(0)
unsigned scene_manager_get_current_scene(SceneManager*s){return s->scene;}
unsigned gui_active_view_port_count(Gui*g,int layer){assert(!depth);return layer==GuiLayerFullscreen?g->full:g->window;}
View* animation_manager_get_animation_view(AnimationManager*a){return &a->view;}
void animation_manager_unload_and_stall_animation(AnimationManager*a){assert(!depth&&!a->stalled);a->stalled=true;freezes++;}
void animation_manager_load_and_continue_animation(AnimationManager*a){assert(!depth&&a->stalled);a->stalled=false;thaws++;}
void view_stack_remove_view(ViewStack*s,View*v){assert(!depth&&s->background==v);s->background=NULL;swaps++;}
void view_stack_add_view(ViewStack*s,View*v){assert(!depth&&!s->background);s->background=v;last_added_stack=s;}
void view_request_update(View*v){assert(!depth&&v);}
void furi_timer_stop(FuriTimer*t){assert(!depth&&t);stops++;}
void furi_timer_flush(void){assert(!depth);flushes++;}
static void desktop_panel_refresh(Desktop*d){assert(!depth&&d);refreshes++;}
bool desktop_panel_display_on(Desktop*d){return d->panel_awake;}
bool desktop_panel_wanted(Desktop*);
void desktop_panel_pause(Desktop*);
void desktop_panel_sync(Desktop*);
void desktop_panel_prepare_lock(Desktop*);
void desktop_panel_after_app(Desktop*);
FavoriteApp* desktop_panel_favorite(Desktop*,FavoriteAppShortcut);
'''


def production():
    harness = HARNESS.replace('"applications/services/desktop/desktop_panel_i.h"',
                              '"' + str(ROOT / 'applications/services/desktop/desktop_panel_i.h') + '"')
    if not SOURCE.exists(): return harness
    source = SOURCE.read_text()
    return harness + '\n'.join(function(source, signature) for signature in (
        'bool desktop_panel_wanted(', 'static void desktop_panel_bind(', 'static void desktop_panel_stop_motion(',
        'void desktop_panel_pause(', 'void desktop_panel_sync(',
        'void desktop_panel_prepare_lock(', 'void desktop_panel_after_app(',
        'FavoriteApp* desktop_panel_favorite(',
    ))


class PanelLifecycleTests(unittest.TestCase):
    def test_startup_lock_app_menu_and_safe_boot_gate(self):
        native(production() + r'''
int main(void){
 (void)desktop_panel_refresh;
 Gui gui={0};SceneManager scene={.scene=DesktopSceneMain};
 Desktop d={.gui=&gui,.scene_manager=&scene,.panel_ready=true,.panel_awake=true};
 d.panel_settings.mode=DesktopModeDashboard;
 assert(desktop_panel_wanted(&d));
 d.panel_ready=false;assert(!desktop_panel_wanted(&d));d.panel_ready=true;
 d.panel_safe_boot=true;assert(!desktop_panel_wanted(&d));d.panel_safe_boot=false;
 d.panel_awake=false;assert(!desktop_panel_wanted(&d));d.panel_awake=true;
 d.locked=true;assert(!desktop_panel_wanted(&d));scene.scene=DesktopSceneLocked;
 assert(desktop_panel_wanted(&d));scene.scene=DesktopScenePinInput;
 assert(!desktop_panel_wanted(&d));scene.scene=DesktopSceneMain;d.locked=false;
 d.app_running=true;assert(!desktop_panel_wanted(&d));d.app_running=false;
 scene.scene=99;assert(!desktop_panel_wanted(&d));scene.scene=DesktopSceneMain;
 gui.full=true;assert(!desktop_panel_wanted(&d));gui.full=false;
 gui.window=true;assert(!desktop_panel_wanted(&d));gui.window=false;
 d.panel_settings.mode=DesktopModeWallpaper;assert(!desktop_panel_wanted(&d));
 return 0;
}
''')

    def test_repeated_mode_lock_and_application_round_trips(self):
        native(production() + r'''
int main(void){
 Gui gui={0};SceneManager scene={.scene=DesktopSceneMain};AnimationManager animation={0};
 ViewStack stack={.background=&animation.view},locked={.background=&animation.view};FuriTimer timer=1;
 Desktop d={.gui=&gui,.scene_manager=&scene,.animation_manager=&animation,.main_view_stack=&stack,
     .locked_view_stack=&locked,.panel_motion_timer=&timer,.panel_ready=true,.panel_awake=true};
 desktop_panel_sync(&d);assert(!freezes&&!thaws&&!d.panel_attached&&!d.panel_model.active);
 for(unsigned i=0;i<50;i++){
  d.panel_settings.mode=DesktopModeDashboard;d.panel_settings.motion=1;
  desktop_panel_sync(&d);assert(d.panel_attached&&d.panel_stalled&&d.panel_model.active);
  assert(stack.background==&d.panel_view&&locked.background==&d.panel_view&&animation.stalled);
  assert(last_added_stack==&stack);
  d.panel_model.page=2;d.panel_model.offset=35;d.panel_timer_armed=true;
  desktop_panel_prepare_lock(&d);d.locked=true;scene.scene=DesktopSceneLocked;
  assert(!d.panel_model.active&&d.panel_stalled&&animation.stalled&&!d.panel_timer_armed);
  assert(!d.panel_model.offset&&d.panel_model.page==2&&stops==i+1&&flushes==i+1);
  desktop_panel_sync(&d);assert(!d.panel_model.active);
  d.locked=false;scene.scene=DesktopSceneMain;desktop_panel_sync(&d);assert(d.panel_model.active&&animation.stalled);
  desktop_panel_pause(&d);d.app_running=true;desktop_panel_sync(&d);
  assert(!d.panel_model.active&&animation.stalled);
  desktop_panel_after_app(&d);assert(!d.app_running&&d.panel_model.active&&animation.stalled);
  gui.full=true;desktop_panel_sync(&d);assert(!d.panel_model.active&&animation.stalled);
  gui.full=false;desktop_panel_sync(&d);assert(d.panel_model.active);
  desktop_panel_pause(&d);d.app_running=true;d.panel_settings.mode=DesktopModeWallpaper;
  desktop_panel_after_app(&d);assert(!d.panel_attached&&!d.panel_model.active&&!animation.stalled);
  assert(stack.background==&animation.view&&locked.background==&animation.view&&!d.panel_stalled);
 }
 assert(freezes==50&&thaws==50&&swaps==200&&refreshes>=100);
 return 0;
}
''')

    def test_held_profiles_are_independent_and_short_press_stays_classic(self):
        native(production() + r'''
int main(void){
 (void)desktop_panel_refresh;
 Desktop d={0};
 for(unsigned i=0;i<FavoriteAppNumber;i++)d.settings.favorite_apps[i].name_or_path[0]='C';
 desktop_panel_defaults(&d.panel_settings,&d.settings);
 d.panel_settings.held[0].name_or_path[0]='P';
 assert(desktop_panel_favorite(&d,FavoriteAppLeftLong)==&d.settings.favorite_apps[FavoriteAppLeftLong]);
 d.panel_attached=true;d.panel_model.active=true;
 assert(desktop_panel_favorite(&d,FavoriteAppLeftLong)==&d.panel_settings.held[0]);
 assert(desktop_panel_favorite(&d,FavoriteAppRightLong)==&d.panel_settings.held[1]);
 assert(desktop_panel_favorite(&d,FavoriteAppOkLong)==&d.panel_settings.held[2]);
 assert(desktop_panel_favorite(&d,FavoriteAppLeftShort)==&d.settings.favorite_apps[FavoriteAppLeftShort]);
 /* First held press after backlight sleep still belongs to the bound Panel. */
 d.panel_model.active=false;
 assert(desktop_panel_favorite(&d,FavoriteAppLeftLong)==&d.panel_settings.held[0]);
 assert(!desktop_panel_favorite(&d,FavoriteAppNumber));
 assert(d.settings.favorite_apps[FavoriteAppLeftLong].name_or_path[0]=='C');
 return 0;
}
''')

    def test_app_return_while_locked_keeps_panels_without_restarting_motion(self):
        native(production() + r'''
int main(void){
 Gui gui={0};SceneManager scene={.scene=DesktopSceneMain};AnimationManager animation={0};
 ViewStack stack={.background=&animation.view},locked={.background=&animation.view};FuriTimer timer=1;
 Desktop d={.gui=&gui,.scene_manager=&scene,.animation_manager=&animation,.main_view_stack=&stack,
     .locked_view_stack=&locked,.panel_motion_timer=&timer,.panel_ready=true,.panel_awake=true};
 d.panel_settings.mode=DesktopModeDashboard;desktop_panel_sync(&d);
 desktop_panel_prepare_lock(&d);d.locked=true;scene.scene=DesktopSceneLocked;
 assert(animation.stalled&&!d.panel_model.active&&locked.background==&d.panel_view);
 d.app_running=true;
 desktop_panel_after_app(&d);
 assert(!d.app_running&&animation.stalled&&d.panel_stalled&&!d.panel_model.active);
 desktop_panel_sync(&d);assert(animation.stalled&&!d.panel_timer_armed);
 d.locked=false;scene.scene=DesktopSceneMain;desktop_panel_sync(&d);assert(animation.stalled&&d.panel_model.active);
 return 0;
}
''')

    def test_classic_locked_app_return_still_restores_the_wallpaper(self):
        native(production() + r'''
int main(void){
 Gui gui={0};SceneManager scene={.scene=DesktopSceneLocked};AnimationManager animation={0};
 ViewStack main={.background=&animation.view},locked={.background=&animation.view};FuriTimer timer=1;
 Desktop d={.gui=&gui,.scene_manager=&scene,.animation_manager=&animation,.main_view_stack=&main,
     .locked_view_stack=&locked,.panel_motion_timer=&timer,.panel_ready=true,.panel_awake=true,.locked=true};
 animation_manager_unload_and_stall_animation(&animation);d.app_running=true;
 desktop_panel_after_app(&d);
 assert(!animation.stalled&&!d.panel_stalled&&!d.panel_attached&&!d.panel_model.active);
 assert(main.background==&animation.view&&locked.background==&animation.view);
 return 0;
}
''')

    def test_initial_panel_binding_while_locked_updates_the_active_stack_last(self):
        native(production() + r'''
int main(void){
 Gui gui={0};SceneManager scene={.scene=DesktopSceneLocked};AnimationManager animation={0};
 ViewStack main={.background=&animation.view},locked={.background=&animation.view};FuriTimer timer=1;
 Desktop d={.gui=&gui,.scene_manager=&scene,.animation_manager=&animation,.main_view_stack=&main,
     .locked_view_stack=&locked,.panel_motion_timer=&timer,.panel_ready=true,.panel_awake=true,.locked=true};
 d.panel_settings.mode=DesktopModeDashboard;d.panel_model.page=2;
 desktop_panel_sync(&d);
 assert(d.panel_attached&&d.panel_stalled&&!d.panel_model.active&&animation.stalled);
 assert(main.background==&d.panel_view&&locked.background==&d.panel_view&&last_added_stack==&locked);
 assert(d.panel_model.page==2&&!d.panel_timer_armed);
 d.panel_safe_boot=true;desktop_panel_sync(&d);
 assert(!d.panel_attached&&!animation.stalled&&main.background==&animation.view&&locked.background==&animation.view);
 return 0;
}
''')


if __name__ == '__main__': unittest.main()
