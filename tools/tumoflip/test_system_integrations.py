"""Production RPC stream ownership and decorative timer gates on host stubs."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_system_storage_activity import function

ROOT = Path(__file__).resolve().parents[2]

COMMON = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "system_policy_i.h"
#define furi_assert assert
#define FURI_LOG_D(...) ((void)0)
"""

RPC = r"""
enum { RpcOwnerBle, RpcOwnerOther, PB_CommandStatus_OK, PB_CommandStatus_ERROR_BUSY,
 PB_CommandStatus_ERROR_VIRTUAL_DISPLAY_ALREADY_STARTED, PB_Main_gui_screen_frame_tag,
 RpcGuiWorkerFlagExit };
typedef struct {size_t size;uint8_t bytes[1024];} PBData;
typedef struct {unsigned command_id,which_content,command_status;
 struct {struct{PBData*data;}gui_screen_frame;}content;} PB_Main;
typedef struct {int owner;} RpcSession;
typedef struct {RpcSession*session;void*gui;bool is_streaming;TumoSystemActivity stream_activity;
 PB_Main*transmit_frame;void*transmit_thread;} RpcGuiSystem;
static TumoSystemState state;
static unsigned workers,streams,reply;
static int PB_Main_msg;
#define PB_BYTES_ARRAY_T_ALLOCSIZE(n) sizeof(PBData)
bool furi_system_policy_acquire(TumoSystemActivity a){return tumo_system_model_acquire(&state,a);}
void furi_system_policy_release(TumoSystemActivity a){tumo_system_model_release(&state,a);}
static int rpc_session_get_owner(RpcSession*s){return s->owner;}
static void rpc_send_and_release_empty(RpcSession*s,unsigned id,unsigned status){(void)s;(void)id;reply=status;}
static size_t gui_get_framebuffer_size(void*g){(void)g;return 1024;}
static void* furi_thread_alloc_ex(const char*n,unsigned size,void*fn,void*ctx){(void)n;(void)size;(void)fn;(void)ctx;workers++;return &workers;}
static void furi_thread_start(void*t){assert(t);}
static void* furi_thread_get_id(void*t){return t;}
static void furi_thread_flags_set(void*t,int flags){(void)flags;assert(t);}
static void furi_thread_join(void*t){assert(t);}
static void furi_thread_free(void*t){assert(t);workers--;}
static void gui_add_framebuffer_callback(void*g,void*cb,void*ctx){(void)g;(void)cb;(void)ctx;streams++;}
static void gui_remove_framebuffer_callback(void*g,void*cb,void*ctx){(void)g;(void)cb;(void)ctx;assert(streams);streams--;}
static void pb_release(void*msg,PB_Main*frame){(void)msg;free(frame->content.gui_screen_frame.data);}
#define rpc_system_gui_screen_stream_frame_transmit_thread NULL
#define rpc_system_gui_screen_stream_frame_callback NULL
"""

RPC_MAIN = r"""
int main(void){
 tumo_system_model_init(&state);RpcSession phone={RpcOwnerBle};RpcGuiSystem gui={.session=&phone};PB_Main req={.command_id=1};
 state.phone_allowed=false;rpc_system_gui_start_screen_stream_process(&req,&gui);
 assert(reply==PB_CommandStatus_ERROR_BUSY&&!workers&&!streams&&!gui.is_streaming);
 state.phone_allowed=true;rpc_system_gui_start_screen_stream_process(&req,&gui);
 assert(reply==PB_CommandStatus_OK&&workers==1&&streams==1&&state.phone_previews==1);
 rpc_system_gui_start_screen_stream_process(&req,&gui);
 assert(reply==PB_CommandStatus_ERROR_VIRTUAL_DISPLAY_ALREADY_STARTED&&workers==1&&state.phone_previews==1);
 assert(tumo_system_model_acquire(&state,TumoSystemWrite));
 assert(!tumo_system_model_quiesce(&state,TumoSystemPhoneDisconnect,0,50));
 tumo_system_model_release(&state,TumoSystemWrite);
 rpc_system_gui_stop_screen_stream_process(&req,&gui);
 rpc_system_gui_stop_screen_stream_process(&req,&gui);
 assert(!state.phone_previews&&!workers&&!streams&&!gui.transmit_frame);
 state.phone_allowed=false;phone.owner=RpcOwnerOther;
 rpc_system_gui_start_screen_stream_process(&req,&gui);assert(state.previews==1&&!state.phone_previews);
 rpc_system_gui_stop_screen_stream_process(&req,&gui);assert(!state.previews&&!workers);
 return 0;
}
"""

BUBBLE = r"""
typedef struct {uint8_t frame_rate;} Icon;
typedef struct {Icon icon_animation;} BubbleAnimation;
typedef struct {const BubbleAnimation*current;Icon*freeze_frame;uint8_t freeze_frame_index,active_shift;} BubbleAnimationViewModel;
typedef struct {bool running;unsigned starts;} FuriTimer;
typedef struct {void*view;FuriTimer*timer;bool visible,quiet;} BubbleAnimationView;
static void furi_timer_start(FuriTimer*t,unsigned ticks){assert(ticks);t->running=true;t->starts++;}
static void furi_timer_stop(FuriTimer*t){t->running=false;}
static void bubble_animation_activate(BubbleAnimationView*v,bool force){(void)v;(void)force;}
static void bubble_animation_activate_right_now(BubbleAnimationView*v){(void)v;assert(0);}
static void bubble_animation_next_frame(BubbleAnimationViewModel*m){(void)m;assert(0);}
static uint8_t icon_get_frame_count(Icon*i){assert(i);return 3;}
static void* view_get_model(void*v){return v;}
static void view_commit_model(void*v,bool update){(void)v;(void)update;}
#define with_view_model(v,decl,body,update) do{decl=(v);body;}while(0)
"""

BUBBLE_MAIN = r"""
int main(void){
 BubbleAnimation frame={.icon_animation={10}};BubbleAnimationViewModel model={.current=&frame};
 FuriTimer timer={0};BubbleAnimationView view={.view=&model,.timer=&timer};
 bubble_animation_timer_start(&view,10);assert(!timer.running);
 bubble_animation_enter(&view);assert(timer.running&&view.visible);
 bubble_animation_set_quiet(&view,true);assert(!timer.running);
 bubble_animation_timer_callback(&view); // A callback already queued before stop must be harmless.
 unsigned before=timer.starts;bubble_animation_timer_start(&view,10);assert(timer.starts==before);
 bubble_animation_set_quiet(&view,false);assert(timer.running);
 bubble_animation_exit(&view);assert(!timer.running&&!view.visible);
 bubble_animation_timer_callback(&view);
 bubble_animation_set_quiet(&view,true);bubble_animation_set_quiet(&view,false);assert(!timer.running);
 frame.icon_animation.frame_rate=0;bubble_animation_enter(&view);assert(!timer.running);
 Icon preview={5};model.freeze_frame=&preview;model.current=NULL;
 bubble_animation_set_quiet(&view,true);bubble_animation_set_quiet(&view,false);assert(timer.running);
 bubble_animation_timer_callback(&view);assert(model.freeze_frame_index==1);
 bubble_animation_set_quiet(&view,true);bubble_animation_timer_callback(&view);assert(model.freeze_frame_index==1);
 assert(model.freeze_frame==&preview&&!model.current); // No frame ownership changes.
 return 0;
}
"""

MANAGER = r"""
#define PROFILE_RELOAD_DEBOUNCE_MS 100
enum {AnimationManagerStateIdle,AnimationManagerStateFreezedIdle};
typedef struct {bool running;unsigned ticks,deadline;} FuriTimer;
typedef struct {bool quiet;} Bubble;
typedef struct {Bubble*animation_view;FuriTimer*idle_animation_timer,*profile_reload_timer;
 unsigned paused_idle_ticks,state;bool quiet,profile_reload_pending;} AnimationManager;
static void bubble_animation_set_quiet(Bubble*v,bool q){v->quiet=q;}
static void furi_timer_start(FuriTimer*t,unsigned ticks){assert(ticks);t->running=true;t->ticks=ticks;}
static void furi_timer_stop(FuriTimer*t){t->running=false;}
static bool furi_timer_is_running(FuriTimer*t){return t->running;}
static unsigned furi_timer_get_expire_time(FuriTimer*t){return t->deadline;}
static unsigned furi_get_tick(void){return 1000;}
static unsigned furi_ms_to_ticks(unsigned n){return n;}
"""

MANAGER_MAIN = r"""
int main(void){
 Bubble decoration={0};FuriTimer idle={.running=true,.deadline=1900},profile={.running=true};
 AnimationManager manager={.animation_view=&decoration,.idle_animation_timer=&idle,
 .profile_reload_timer=&profile,.profile_reload_pending=true};
 animation_manager_set_quiet(&manager,true);
 assert(manager.quiet&&decoration.quiet&&!idle.running&&!profile.running&&manager.paused_idle_ticks==900);
 animation_manager_idle_start(&manager,700);assert(!idle.running&&manager.paused_idle_ticks==700);
 animation_manager_set_quiet(&manager,false);
 assert(!decoration.quiet&&idle.running&&idle.ticks==700&&profile.running&&profile.ticks==100);
 manager.state=AnimationManagerStateFreezedIdle;animation_manager_set_quiet(&manager,true);
 animation_manager_set_quiet(&manager,false);assert(!idle.running); // No background resume behind an app.
 return 0;
}
"""

DESKTOP = r"""
enum {DesktopSceneMain,DesktopSceneLocked,DesktopSceneActivity,DesktopSceneQuickSettings};
enum {DesktopModeDashboard=1};
typedef struct {bool quiet,locked,app_running,recovery_notice_pending;
 struct{unsigned mode;}panel;
 void*animation_manager,*scene_manager,*view_dispatcher,*activity;} Desktop;
static TumoSystemState state;
static unsigned tick_period,notices,clock_updates,activity_updates;
TumoSystemState furi_system_policy_snapshot(void){return state;}
static void animation_manager_set_quiet(void*m,bool q){*(bool*)m=q;}
static void desktop_clock_reconfigure(Desktop*d){assert(d);clock_updates++;}
static unsigned scene_manager_get_current_scene(void*m){return *(unsigned*)m;}
static void*view_dispatcher_get_event_loop(void*d){return d;}
static void desktop_tick_event_callback(void*d){(void)d;}
static void furi_event_loop_tick_set(void*loop,unsigned period,void(*cb)(void*),void*ctx){assert(loop&&cb&&ctx);tick_period=period;}
static unsigned furi_hal_power_insomnia_level(void){return 7;}
static void desktop_activity_update(void*a,const TumoSystemState*s,unsigned holds){assert(a&&s&&holds==7);activity_updates++;}
static void scene_manager_next_scene(void*m,unsigned scene){assert(scene==DesktopSceneActivity);*(unsigned*)m=scene;notices++;}
static void desktop_dashboard_refresh(Desktop*d,const TumoSystemState*s,unsigned scene){(void)d;(void)s;(void)scene;}
"""

DESKTOP_MAIN = r"""
int main(void){
 bool decoration=false;unsigned scene=DesktopSceneMain;
 Desktop desktop={.animation_manager=&decoration,.scene_manager=&scene,.view_dispatcher=&scene,.activity=&scene};
 tumo_system_model_init(&state);desktop_update_policy(&desktop);assert(tick_period==500&&!decoration);
 state.display_on=false;state.writes=1;desktop_update_policy(&desktop);
 assert(tick_period==5000&&decoration&&state.writes==1); // Decoration only, writing continues.
 state.phone_previews=1;desktop_update_policy(&desktop);assert(tick_period==500&&!decoration);
 state.phone_previews=0;state.previews=1;desktop_update_policy(&desktop);assert(tick_period==500&&!decoration);
 state.previews=0;scene=DesktopSceneQuickSettings;desktop_update_policy(&desktop);assert(tick_period==500&&decoration);
 scene=DesktopSceneLocked;desktop.locked=true;desktop.recovery_notice_pending=true;state.recovery=true;
 desktop_update_policy(&desktop);assert(!notices&&tick_period==5000);
 scene=DesktopSceneMain;desktop_update_policy(&desktop);assert(!notices); // PIN still locked.
 desktop.locked=false;desktop.app_running=true;desktop_update_policy(&desktop);assert(!notices);
 desktop.app_running=false;desktop_update_policy(&desktop);assert(notices==1&&scene==DesktopSceneActivity);
 desktop_update_policy(&desktop);assert(tick_period==500&&activity_updates==1&&clock_updates>=3);
 return 0;
}
"""


class SystemIntegrationTests(unittest.TestCase):
    def native(self, source):
        with tempfile.TemporaryDirectory(prefix="system-integration-") as temp:
            root = Path(temp)
            (root / "test.c").write_text(COMMON + source)
            result = subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-iquote", str(ROOT / "furi/core"), str(root / "test.c"),
                str(ROOT / "furi/core/system_policy_model.c"), "-o", str(root / "test"),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(root / "test")], check=True, timeout=10)

    def test_actual_rpc_start_stop_balance_and_admission(self):
        source = (ROOT / "applications/services/rpc/rpc_gui.c").read_text()
        self.native(RPC + "\n".join(function(source, name) for name in (
            "rpc_system_gui_start_screen_stream_process", "rpc_system_gui_stop_screen_stream_process")) + RPC_MAIN)

    def test_actual_bubble_timers_respect_visibility_quiet_and_zero_rate(self):
        source = (ROOT / "applications/services/desktop/animations/views/bubble_animation_view.c").read_text()
        self.native(BUBBLE + "\n".join(function(source, name) for name in (
            "bubble_animation_timer_start", "bubble_animation_timer_callback", "bubble_animation_enter", "bubble_animation_exit",
            "bubble_animation_set_quiet")) + BUBBLE_MAIN)

    def test_actual_manager_pauses_timers_and_keeps_pending_profile_work(self):
        source = (ROOT / "applications/services/desktop/animations/animation_manager.c").read_text()
        self.native(MANAGER + "\n".join(function(source, name) for name in (
            "animation_manager_idle_start", "animation_manager_set_quiet")) + MANAGER_MAIN)

    def test_desktop_wake_remote_preview_and_recovery_never_bypass_pin(self):
        source = (ROOT / "applications/services/desktop/desktop.c").read_text()
        self.native(DESKTOP + function(source, "desktop_update_policy") + DESKTOP_MAIN)


if __name__ == "__main__":
    unittest.main()
