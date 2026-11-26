"""Actual dashboard input/motion handlers; no FAP or device is launched."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from tools.tumoflip.render_system_activity import build_source

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
int main(void){
 DesktopActivityModel m={0};DesktopActivityView activity={.view=&m,.callback=action,.context=&events};
 TumoSystemState state;tumo_system_model_init(&state);state.phone_previews=1;
 desktop_activity_update(&activity,&state,0);desktop_activity_set_home(&activity,true);
 InputEvent left={InputTypeShort,InputKeyLeft},right={InputTypeShort,InputKeyRight},ok={InputTypeShort,InputKeyOk};
 assert(!desktop_activity_input(&ok,&activity)&&!m.confirm&&!events); // Home OK must remain Apps, not Pause.
 system_view_tick=100;assert(desktop_activity_input(&left,&activity));
 assert(m.page==2&&events==1&&last_action==DesktopActivityHomeChanged);
 assert(desktop_activity_home_tick(&activity,100,true,true)==40&&m.offset==-112);
 assert(desktop_activity_home_tick(&activity,180,true,true)==40&&m.offset==-56);
 assert(!desktop_activity_home_tick(&activity,260,true,true)&&!m.offset);
 unsigned count=events;uint8_t page=m.page;
 assert(!desktop_activity_input(&(InputEvent){InputTypeLong,InputKeyLeft},&activity));
 assert(!desktop_activity_input(&(InputEvent){InputTypeLong,InputKeyRight},&activity));
 assert(!desktop_activity_input(&(InputEvent){InputTypeRepeat,InputKeyRight},&activity));
 assert(m.page==page&&events==count); // Held shortcuts fall through, no accidental paging.
 system_view_tick=300;desktop_activity_input(&right,&activity);assert(m.page==0);
 system_view_tick=330;desktop_activity_input(&right,&activity);assert(m.page==1);
 assert(desktop_activity_home_tick(&activity,330,true,true)==40&&m.offset==112);
 assert(desktop_activity_home_tick(&activity,500,true,true)==250&&!m.offset);
 assert(m.icon_frame<8);assert(!desktop_activity_home_tick(&activity,550,false,true)&&!m.offset&&!m.icon_frame);
 state.display_on=false;desktop_activity_update(&activity,&state,0);
 assert(!desktop_activity_home_tick(&activity,600,true,true));
 state.display_on=true;state.recovery=true;desktop_activity_update(&activity,&state,0);
 assert(!desktop_activity_home_tick(&activity,650,true,true));
 state.recovery=false;desktop_activity_update(&activity,&state,0);
 assert(!desktop_activity_home_tick(&activity,700,true,false)); // Animations can be disabled without losing pages.
 desktop_activity_set_home(&activity,false);m.page=0;
 desktop_activity_input(&ok,&activity);assert(m.confirm); // Separate system screen still has confirmation controls.
 return 0;
}
'''


class DesktopDashboardTests(unittest.TestCase):
    def test_real_home_input_fallthrough_retarget_and_timer_admission(self):
        source = build_source().split("int main(int argc,char**argv){", 1)[0] + DRIVER
        with tempfile.TemporaryDirectory(prefix="dashboard-native-") as directory:
            work = Path(directory)
            (work / "case.c").write_text(source)
            sources = [path for path in (ROOT / "lib/u8g2").glob("*.c") if path.name != "u8g2_glue.c"]
            compiled = subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
                "-ffunction-sections", "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                "-I", str(ROOT), "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                "-iquote", str(ROOT / "furi/core"), str(work / "case.c"),
                str(ROOT / "furi/core/system_policy_model.c"), *map(str, sources), "-lm", "-o", str(work / "test"),
            ], capture_output=True, text=True, timeout=40)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(work / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
