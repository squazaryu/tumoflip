"""Render/test the production core activity screen on synthetic 128x64 models."""
import argparse
import re
import subprocess
import tempfile
import sys
from pathlib import Path
from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as gui

ROOT = Path(__file__).resolve().parents[2]
VIEW = ROOT / "applications/services/desktop/views/desktop_view_activity.c"


def build_source():
    source = VIEW.read_text()
    source = re.sub(r"(static void|uint32_t)\s+(desktop_activity_\w+)\(", r"\1 \2(", source)
    header = (VIEW.with_suffix(".h")).read_text()
    canvas = (ROOT / "applications/services/gui/canvas.c").read_text()
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    support = gui.SUPPORT.replace(
        'typedef struct {size_t width,height;const uint8_t* data;} Icon;',
        'typedef struct {size_t width,height;const uint8_t* data;uint8_t frame_count;const uint8_t*const*frames;} Icon;')
    support = support.replace('u8g2_DrawXBMP(&c->fb,x,y,', 'u8g2_DrawXBMP(&c->fb,x+c->offset_x,y+c->offset_y,')
    chunks = [support, '#include "system_policy_i.h"\n#include <stddef.h>',
              '#include "applications/services/desktop/desktop_panel_i.h"\n'
              'static uint32_t system_view_tick;\nstatic uint32_t furi_get_tick(void){return system_view_tick;}']
    chunks.append(canvas[canvas.index("const CanvasFontParameters canvas_font_params"):canvas.index("Canvas* canvas_init")])
    for name in ("void canvas_clear(", "void canvas_set_color(", "void canvas_invert_color(",
                 "void canvas_set_font(", "void canvas_draw_str(", "void canvas_draw_str_aligned(",
                 "uint16_t canvas_string_width(", "void canvas_draw_box(", "void canvas_draw_line(",
                 "void canvas_draw_rframe("):
        body = gui.function(canvas, name)
        if name in ("void canvas_draw_str(", "void canvas_draw_str_aligned("):
            body = body.replace("    u8g2_DrawUTF8", '    int32_t lx=x-canvas->offset_x,ly=y-canvas->offset_y;\n'
                                '    if(lx<0 || lx+u8g2_GetUTF8Width(&canvas->fb,str)>128) fprintf(stderr,"Text bounds: %s x=%d width=%d\\n",str,lx,u8g2_GetUTF8Width(&canvas->fb,str));\n'
                                '    assert(lx>=0 && lx+u8g2_GetUTF8Width(&canvas->fb,str)<=128);\n'
                                '    assert(ly<=64 && ly-u8g2_GetAscent(&canvas->fb)>=13);\n    u8g2_DrawUTF8')
        chunks.append(body)
    chunks.append(gui.function(elements, "void elements_button_center("))
    chunks.append(header[header.index("typedef enum"):header.index("typedef struct DesktopActivityView")])
    chunks.append("typedef struct DesktopActivityView DesktopActivityView;")
    chunks.append(source[source.index("typedef struct"):source.index("static void desktop_activity_icon")])
    chunks.append(r'''
enum { InputTypeShort, InputTypeRepeat, InputTypeLong,InputKeyBack,InputKeyLeft,InputKeyRight,InputKeyOk };
typedef struct {int type,key;} InputEvent;
typedef void View;
static size_t icon_get_width(const Icon*i){return i->width;}
static size_t icon_get_height(const Icon*i){return i->height;}
static uint8_t icon_get_frame_count(const Icon*i){return i->frame_count;}
static const uint8_t*icon_get_frame_data(const Icon*i,uint8_t n){assert(n<i->frame_count);return i->frames[n];}
static void canvas_draw_bitmap(Canvas*c,int32_t x,int32_t y,size_t w,size_t h,const uint8_t*data){
 u8g2_DrawXBMP(&c->fb,x+c->offset_x,y+c->offset_y,w,h,data);
}
static void canvas_frame_set(Canvas*c,int32_t x,int32_t y,size_t w,size_t h){
 assert(w==128&&h==64);c->offset_x=x;c->offset_y=y;
}
''')
    # View is opaque; only the real input/draw/update handlers run here, not hardware allocation.
    chunks[-2] = chunks[-2].replace("View* view;", "void* view;")
    chunks.append(gui.function(source, "static void desktop_activity_icon("))
    badge = gui.function(source, "static void desktop_activity_badge(")
    badge = badge.replace("    canvas_draw_rframe", '    canvas_set_font(canvas,FontSecondary);\n'
        '    assert(icon_get_width(icon)+2+canvas_string_width(canvas,label)<=38);\n'
        '    canvas_draw_rframe', 1)
    badge = badge.replace("    canvas_draw_str_aligned", '    assert(canvas_string_width(canvas,value)<=36);\n'
        '    canvas_draw_str_aligned', 1)
    chunks.append(badge)
    chunks += [gui.function(source, name) for name in (
        "static void desktop_activity_hero(", "static void desktop_activity_draw_page(", "static void desktop_activity_draw(",
        "static bool desktop_activity_input(", "void desktop_activity_reset(",
        "void desktop_activity_update(", "void desktop_activity_blocked(")]
    for signature in ("void desktop_activity_set_home(", "uint32_t desktop_activity_home_tick("):
        if signature in source:
            chunks.append(gui.function(source, signature))
    text = "\n".join(chunks)
    preview_bar = r'''
/* Stock Desktop bar on synthetic states; not an observation of a connected device. */
static void preview_status_bar(Canvas*c){
 canvas_draw_icon(c,0,0,&I_Background_128x11);
 canvas_draw_icon(c,2,1,&I_Bluetooth_Idle_5x8);
 canvas_draw_icon(c,100,1,&I_Battery_26x8);
}
'''
    icons = []
    for name in sorted(set(re.findall(r"\b(?:I|A)_[A-Za-z0-9_]+", text + preview_bar))):
        path = (next((ROOT / "assets/icons").rglob(name[2:])) / "frame_01.png" if name.startswith("A_")
                else next((ROOT / "assets/icons").rglob(name[2:] + ".png")))
        frames = sorted(path.parent.glob("frame_*.png")) if name.startswith("A_") else [path]
        for index, frame in enumerate(frames):
            image = Image.open(frame).convert("1")
            width, height = image.size
            bits = [sum(1 << bit for bit in range(8) if x * 8 + bit < width and
                        image.getpixel((x * 8 + bit, y)) == 0)
                    for y in range(height) for x in range((width + 7) // 8)]
            icons.append(f'static const uint8_t {name}_bits{index}[]={{' + ",".join(map(str, bits)) + "};")
        icons.append(f'static const uint8_t*const {name}_frames[]={{' + ",".join(f"{name}_bits{i}" for i in range(len(frames))) + "};")
        icons.append(f'static const Icon {name}={{{width},{height},{name}_bits0,{len(frames)},{name}_frames}};')
    # Icon is defined in SUPPORT, before any production draw function.
    text = text.replace("const CanvasFontParameters canvas_font_params", "\n".join(icons) + "\nconst CanvasFontParameters canvas_font_params", 1)
    text += preview_bar
    text += r'''
static unsigned events;
static DesktopActivityAction last_action;
static void action(DesktopActivityAction a,void*context){assert(context==&events);last_action=a;events++;}
static void save(Canvas*c,const char*dir,unsigned index){
 preview_status_bar(c);
 char path[2048];snprintf(path,sizeof(path),"%s/%02u.pgm",dir,index);FILE*f=fopen(path,"wb");assert(f);
 fprintf(f,"P5\n128 64\n255\n");
 for(int y=0;y<64;y++)for(int x=0;x<128;x++){uint8_t p=(c->fb.tile_buf_ptr[(y/8)*128+x]>>(y%8)&1)?0:255;fwrite(&p,1,1,f);}fclose(f);
}
int main(int argc,char**argv){
 assert(argc==2);Canvas c={0};uint8_t pixels[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,pixels,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_color(&c,ColorBlack);
 DesktopActivityModel m={0};DesktopActivityView activity={.view=&m,.callback=action,.context=&events};
 TumoSystemState state;tumo_system_model_init(&state);
 state.phone_previews=1;desktop_activity_update(&activity,&state,0);
 InputEvent ok={InputTypeShort,InputKeyOk},right={InputTypeShort,InputKeyRight},left={InputTypeShort,InputKeyLeft};
 desktop_activity_draw(&c,&m);save(&c,argv[1],0);
 assert(!desktop_activity_input(&(InputEvent){InputTypeShort,InputKeyBack},&activity));
 desktop_activity_input(&ok,&activity);assert(m.confirm&&!events);
 desktop_activity_draw(&c,&m);save(&c,argv[1],1);
 desktop_activity_input(&right,&activity);assert(!m.confirm&&m.page==1&&!events);
 desktop_activity_draw(&c,&m);save(&c,argv[1],2);
 desktop_activity_input(&left,&activity);desktop_activity_input(&ok,&activity);desktop_activity_input(&ok,&activity);
 assert(events==1&&last_action==DesktopActivityPausePhone&&!m.confirm);
 desktop_activity_blocked(&activity);assert(m.blocked);
 desktop_activity_draw(&c,&m);save(&c,argv[1],3);
 desktop_activity_input(&ok,&activity);assert(!m.blocked&&!m.confirm&&events==1);
 state.phone_allowed=false;state.phone_previews=0;desktop_activity_reset(&activity,false);desktop_activity_update(&activity,&state,0);
 desktop_activity_draw(&c,&m);save(&c,argv[1],4);
 desktop_activity_input(&ok,&activity);assert(events==2&&last_action==DesktopActivityAllowPhone);
 desktop_activity_input(&left,&activity);assert(events==3&&last_action==DesktopActivityPrevious);
 state.recovery=true;state.boot_crashes=3;desktop_activity_reset(&activity,true);desktop_activity_update(&activity,&state,65535);
 desktop_activity_draw(&c,&m);save(&c,argv[1],5);
 desktop_activity_input(&ok,&activity);assert(m.confirm&&events==3);
 desktop_activity_draw(&c,&m);save(&c,argv[1],6);
 desktop_activity_input(&ok,&activity);assert(events==4&&last_action==DesktopActivityRestart);
 m.page=1;desktop_activity_draw(&c,&m);save(&c,argv[1],7);
 m.page=0;state.writes=state.phone_previews=state.previews=255;state.phone_allowed=true;state.transfer=true;
 desktop_activity_update(&activity,&state,65535);desktop_activity_draw(&c,&m);save(&c,argv[1],8);
 m.page=2;state.recovery=false;state.boot_crashes=0;desktop_activity_update(&activity,&state,0);
 desktop_activity_draw(&c,&m);save(&c,argv[1],9);
 desktop_activity_input(&ok,&activity);assert(!m.confirm&&events==4);
 desktop_activity_input(&right,&activity);assert(m.page==0);
 return 0;
}
'''
    return text


def render(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="core-activity-ui-") as directory:
        work = Path(directory)
        (work / "render.c").write_text(build_source())
        compiled = subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
            "-ffunction-sections", "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections", "-I", str(ROOT / "lib/u8g2"),
            "-I", str(ROOT / "lib/mlib"), "-iquote", str(ROOT / "furi/core"),
            "-I", str(ROOT),
            str(work / "render.c"), str(ROOT / "furi/core/system_policy_model.c"),
            *[str(path) for path in (ROOT / "lib/u8g2").glob("u8g2_*.c") if path.name != "u8g2_glue.c"],
            *map(str, (ROOT / "lib/u8g2").glob("u8x8_*.c")), "-lm", "-o", str(work / "render")],
            capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)
        subprocess.run([str(work / "render"), str(output.resolve())], check=True, timeout=15)
    frames = sorted(output.glob("*.pgm"))
    assert len(frames) == 10
    sheet = Image.new("RGB", (548, 5 * 164), "#eee")
    draw = ImageDraw.Draw(sheet)
    labels = ["Activity", "Pause confirmation", "Idle / sleep", "Busy / denied",
              "Preview paused", "Recovery", "Restart confirmation", "Recovery idle",
              "Maximum counters", "Normal boot"]
    for i, frame in enumerate(frames):
        image = Image.open(frame).convert("RGB")
        x, y = (i % 2) * 274 + 8, (i // 2) * 164 + 22
        draw.text((x, y - 18), labels[i], fill="black")
        sheet.paste(image.resize((256, 128), Image.Resampling.NEAREST), (x, y))
    sheet.save(output / "system-activity.png")
    print("10 production UI states; input assertions and 128x64 text bounds passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    render(parser.parse_args().output)
