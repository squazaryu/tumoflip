"""Run actual Panel controller bodies with cached services and deterministic time."""
from pathlib import Path
import unittest
from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.render_weather_native import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "applications/services/desktop/desktop_panel.c"

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "PANEL_HEADER"
typedef struct {unsigned key,type;} InputEvent;
enum {InputKeyLeft,InputKeyRight,InputKeyOk,InputTypeShort=3,InputTypeLong,
 DesktopPanelEventPrevious=100,DesktopPanelEventNext,ViewModelTypeLockFree=1,
 FuriTimerTypePeriodic=2,LayerMAX=2};
static const unsigned gpio_button_back=0;
typedef struct {unsigned model_type;void*model;void*context;void*draw;void*input;} View;
typedef struct {bool armed;uint32_t period;void(*callback)(void*);void*context;} FuriTimer;
typedef struct {struct {unsigned index;uint8_t value[2];} display;} Notification;
typedef struct {unsigned kind;uint32_t commit;} TumoflipCrashReport;
typedef struct {
 DesktopSettings settings;DesktopPanelSettings panel_settings;DesktopPanelModel panel_model;
 View panel_view;FuriTimer*panel_motion_timer;Notification*notification;
 bool panel_attached,panel_stalled,panel_timer_armed,panel_ready,panel_safe_boot;
 uint8_t panel_sd_state;void*view_dispatcher;
} Desktop;
static unsigned depth,updates,events,last_event,starts,stops,loads;
static uint32_t tick,heap=131071;static uint16_t holds;
static bool radio,back_high=true,have_fault,have_profile;
static DesktopPanelSettings stored;static FuriTimer timer;static const void*capability;
#define FURI_CRITICAL_ENTER() ++depth
#define FURI_CRITICAL_EXIT() do{assert(depth);--depth;}while(0)
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t c=l<n?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
size_t memmgr_get_free_heap(void){assert(!depth);return heap;}
uint16_t furi_hal_power_insomnia_level(void){assert(!depth);return holds;}
bool furi_hal_bt_is_active(void){assert(!depth);return radio;}
bool tumoflip_crash_journal_read(TumoflipCrashReport*r){assert(!depth);r->kind=1;r->commit=0x12345678;return have_fault;}
const char* tumoflip_crash_journal_kind_name(unsigned k){assert(k==1);return "Extremely long synthetic fault name";}
void view_request_update(View*v){assert(!depth&&v);updates++;}
void desktop_panel_stop_motion(Desktop*d){assert(!depth);if(d->panel_timer_armed){stops++;d->panel_timer_armed=false;timer.armed=false;}}
uint32_t furi_get_tick(void){return tick;}
uint32_t furi_ms_to_ticks(uint32_t ms){return ms;}
void furi_timer_start(FuriTimer*t,uint32_t period){assert(!depth&&t==&timer);starts++;t->armed=true;t->period=period;}
void view_dispatcher_send_custom_event(void*d,unsigned event){(void)d;assert(!depth);events++;last_event=event;}
bool desktop_panel_load(DesktopPanelSettings*p){assert(!depth);loads++;if(have_profile)*p=stored;return have_profile;}
bool furi_hal_gpio_read(const void*p){(void)p;assert(!depth);return back_high;}
void view_init(View*v){memset(v,0,sizeof(*v));}
void view_set_context(View*v,void*c){v->context=c;}
void view_set_draw_callback(View*v,void(*c)(void*,void*)){v->draw=(void*)c;}
void view_set_input_callback(View*v,bool(*c)(InputEvent*,void*)){v->input=(void*)c;}
void desktop_panel_draw(void*c,void*m){(void)c;(void)m;}
FuriTimer* furi_timer_alloc(void(*c)(void*),unsigned type,void*d){assert(type==FuriTimerTypePeriodic&&!depth);timer.callback=c;timer.context=d;return &timer;}
void furi_record_create(const char*name,void*p){assert(!depth&&!strcmp(name,RECORD_DESKTOP_PANEL));capability=p;}
'''


def production():
    source = SOURCE.read_text()
    result = HARNESS.replace("PANEL_HEADER", str(ROOT / "applications/services/desktop/desktop_panel_i.h"))
    for signature in ("static bool desktop_panel_display_on(", "static void desktop_panel_refresh(",
                      "static void desktop_panel_motion_tick(", "void desktop_panel_move(",
                      "static bool desktop_panel_input(", "void desktop_panel_reload(", "void desktop_panel_init("):
        result += "\n" + function(source, signature).replace("static bool", "bool", 1).replace("static void", "void", 1)
    return result


class PanelRuntimeTests(unittest.TestCase):
    def test_init_is_inert_and_profile_reload_has_no_write_path(self):
        native(production() + r'''
int main(void){
 Desktop d={0};memset(&d.settings,0x7f,sizeof(d.settings));back_high=false;
 desktop_panel_init(&d);
 assert(!d.panel_ready&&!d.panel_model.active&&!timer.armed&&!starts&&!loads&&!events);
 assert(d.panel_safe_boot&&d.panel_settings.mode==DesktopModeWallpaper);
 assert(d.panel_motion_timer==&timer&&timer.context==&d&&timer.callback);
 assert(capability&&*(const uint32_t*)capability==2);
 assert(d.panel_view.model==&d.panel_model&&d.panel_view.context==&d);
 memset(&d.settings,0,sizeof(d.settings));strcpy(d.settings.favorite_apps[FavoriteAppLeftLong].name_or_path,"Classic");
 desktop_panel_reload(&d);assert(loads==1&&!strcmp(d.panel_settings.held[0].name_or_path,"Classic"));
 stored=d.panel_settings;stored.mode=DesktopModeDashboard;strcpy(stored.held[0].name_or_path,"Panel");have_profile=true;
 desktop_panel_reload(&d);assert(loads==2&&d.panel_settings.mode==DesktopModeDashboard);
 assert(!strcmp(d.panel_settings.held[0].name_or_path,"Panel"));
 assert(!strcmp(d.settings.favorite_apps[FavoriteAppLeftLong].name_or_path,"Classic"));
 return 0;
}
''')

    def test_motion_tick_never_posts_events_or_starts_a_timer(self):
        native(production() + r'''
int main(void){
 Desktop d={0};desktop_panel_init(&d);d.panel_model.active=true;d.panel_model.motion=true;
 tick=UINT32_MAX-79;desktop_panel_move(&d,1);
 assert(d.panel_model.page==1&&d.panel_model.offset==112&&timer.armed&&timer.period==40);
 for(unsigned i=0;i<4;i++){tick+=40;timer.callback(timer.context);}
 assert(!d.panel_model.offset&&!d.panel_model.direction&&starts==1&&!events);
 unsigned before=updates;timer.callback(timer.context);assert(updates==before);
 d.panel_model.motion=false;desktop_panel_move(&d,-1);
 assert(d.panel_model.page==0&&!d.panel_model.offset&&!timer.armed&&stops==1);
 d.panel_model.active=false;before=updates;desktop_panel_move(&d,1);assert(updates==before&&starts==1);
 InputEvent key={.key=InputKeyLeft,.type=InputTypeShort};
 assert(desktop_panel_input(&key,&d)&&events==1&&last_event==DesktopPanelEventPrevious);
 key.key=InputKeyRight;assert(desktop_panel_input(&key,&d)&&last_event==DesktopPanelEventNext);
 key.type=InputTypeLong;assert(!desktop_panel_input(&key,&d));
 key.type=InputTypeShort;key.key=InputKeyOk;assert(!desktop_panel_input(&key,&d)&&events==2);
 return 0;
}
''')

    def test_cached_status_unknown_fields_and_redraw_loop_prevention(self):
        native(production() + r'''
int main(void){
 Notification notification={.display={.index=0,.value={0,1}}};Desktop d={.notification=&notification};
 assert(!desktop_panel_display_on(&d));notification.display.index=1;assert(desktop_panel_display_on(&d));
 notification.display.index=LayerMAX;assert(!desktop_panel_display_on(&d));
 d.panel_sd_state=0;desktop_panel_refresh(&d);
 assert(d.panel_model.snapshot.sd_state==0&&d.panel_model.snapshot.heap_kb==127&&!d.panel_model.snapshot.fault_valid);
 unsigned before=updates;holds=2;desktop_panel_refresh(&d);assert(updates==before&&d.panel_model.snapshot.sleep_holds==2);
 radio=true;d.panel_sd_state=2;have_fault=true;d.settings.auto_lock_delay_ms=60000;
 desktop_panel_refresh(&d);assert(updates==before+1);
 DesktopPanelSnapshot*s=&d.panel_model.snapshot;
 assert(s->bt_on&&s->sd_state==2&&s->auto_lock_s==60&&s->fault_valid&&s->fault_commit==0x12345678);
 assert(s->fault_name[sizeof(s->fault_name)-1]==0);
 before=updates;desktop_panel_refresh(&d);assert(updates==before);
 d.panel_sd_state=1;desktop_panel_refresh(&d);assert(updates==before+1&&!depth);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
