"""Render actual local Panel C, repository U8g2/fonts/icons, synthetic status only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from tools.tumoflip.render_weather_native import ROOT, SUPPORT, MODELS, function

SOURCE = ROOT / "applications/services/desktop/views/desktop_view_panel.c"

DRIVER = r'''
static void save(Canvas* c,const char* dir,const char* name){
 char path[2048];snprintf(path,sizeof(path),"%s/%s.pgm",dir,name);
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P5\n128 64\n255\n");
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++){
  uint8_t p=(c->fb.tile_buf_ptr[(y/8)*128+x]>>(y%8)&1)?0:255;
  assert(fwrite(&p,1,1,f)==1);
 }assert(!fclose(f));
}
static void render(Canvas*c,DesktopPanelModel*m,const char*dir,const char*name){
 canvas_clear(c);canvas_set_color(c,ColorBlack);bounds_check=m->offset==0;
 desktop_panel_draw(c,m);assert(c->offset_x==0&&c->offset_y==0);
 assert(c->width==128&&c->height==64);save(c,dir,name);
}
int main(int argc,char**argv){
 assert(argc==2);Canvas c={.width=128,.height=64};static uint8_t buffer[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;
 u8g2_SetupBuffer(&c.fb,buffer,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 DesktopPanelModel m={.active=true,.snapshot={.heap_kb=135,.sd_state=2,.bt_on=true,.auto_lock_s=60}};
 render(&c,&m,argv[1],"01-status");
 m.snapshot.sd_state=1;m.snapshot.bt_on=false;m.snapshot.heap_kb=0;
 render(&c,&m,argv[1],"02-status-no-card");
 m.snapshot.sd_state=255;m.snapshot.heap_kb=UINT16_MAX;
 render(&c,&m,argv[1],"03-status-unknown");
 m.page=1;m.snapshot.auto_lock_s=0;m.snapshot.sleep_holds=0;
 render(&c,&m,argv[1],"04-idle-off");
 m.snapshot.auto_lock_s=UINT16_MAX;m.snapshot.sleep_holds=UINT16_MAX;
 render(&c,&m,argv[1],"05-idle-max");
 m.page=2;render(&c,&m,argv[1],"06-fault-unavailable");
 m.snapshot.fault_valid=true;m.snapshot.fault_commit=0x85F6E39D;
 strcpy(m.snapshot.fault_name,"Hard fault");
 render(&c,&m,argv[1],"07-last-fault");
 memset(m.snapshot.fault_name,'W',sizeof(m.snapshot.fault_name));
 render(&c,&m,argv[1],"08-fault-long");
 m.page=255;m.snapshot.sd_state=2;render(&c,&m,argv[1],"09-invalid-page");
 m.page=1;m.snapshot.auto_lock_s=60;m.snapshot.sleep_holds=2;
 for(int i=0;i<5;i++){
  char name[32];snprintf(name,sizeof(name),"10-slide-%d",i);
  m.offset=desktop_panel_offset(0,i*40,1,true);render(&c,&m,argv[1],name);
 }
 m.page=0;
 for(int i=0;i<5;i++){
  char name[32];snprintf(name,sizeof(name),"11-slide-left-%d",i);
  m.offset=desktop_panel_offset(0,i*40,-1,true);render(&c,&m,argv[1],name);
 }
 /* Dispatcher state must be restored, including when the model is paused. */
 bounds_check=false;c.offset_x=3;c.offset_y=2;c.width=120;c.height=60;
 m.active=false;m.offset=112;desktop_panel_draw(&c,&m);
 assert(c.offset_x==3&&c.offset_y==2&&c.width==120&&c.height==60);
 c.offset_x=c.offset_y=0;c.width=128;c.height=64;render_panel_settings(&c,argv[1]);
 assert(!critical_depth&&text_calls>100);return 0;
}
'''


def settings_chunks(elements):
    """Actual list renderer; controlled display fixtures from production labels."""
    scene = (ROOT / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c").read_text()
    labels = [re.search(r'variable_item_list,\s*"([^"]+)",\s*2,\s*' + callback, scene)[1]
              for callback in ("desktop_settings_scene_start_mode_changed",
                               "desktop_settings_scene_start_shortcut_set_changed",
                               "desktop_settings_scene_start_panel_motion_changed")]
    model = MODELS[:MODELS.index("typedef struct {FuriString* label;size_t index;")]
    chunks = [model]
    for signature in ("void elements_scrollbar_pos(", "void elements_scrollbar(",
                      "void elements_slightly_rounded_box(", "void elements_slightly_rounded_frame(",
                      "void elements_string_fit_width(", "void elements_scrollable_text_line_str(",
                      "static size_t\n    elements_get_max_chars_to_fit(",
                      "void elements_multiline_text_aligned("):
        chunks.append(function(elements, signature))
    source = (ROOT / "applications/services/gui/modules/variable_item_list.c").read_text()
    chunks.append(function(source, "static void variable_item_list_draw_callback("))
    chunks.append(r'''
static void save(Canvas*,const char*,const char*);
static void panel_choices(Canvas*c,const char*dir,const char*name,const char**labels,
                          const char**values,const uint8_t*indices,const uint8_t*counts,
                          unsigned n,unsigned selected,unsigned window){
 VariableItemListModel m={0};VariableItemArray_init(m.items);m.position=selected;m.window_position=window;
 for(unsigned i=0;i<n;i++){
  VariableItem item={.label=labels[i],.current_value_index=indices[i],
                    .current_value_text=furi_string_alloc_set(values[i]),.values_count=counts[i]};
  VariableItemArray_push_back(m.items,item);
 }
 canvas_set_color(c,ColorBlack);minimum_text_y=0;bounds_check=true;
 variable_item_list_draw_callback(c,&m);save(c,dir,name);minimum_text_y=20;
 for(unsigned i=0;i<n;i++)furi_string_free(VariableItemArray_get(m.items,i)->current_value_text);
 VariableItemArray_clear(m.items);
}
static void render_panel_settings(Canvas*c,const char*dir){
 const char* labels[]={"PIN Setup",LABELS};
 const char* classic[]={"","Classic","Classic","On"};
 const char* panel[]={"","Panel","Panel","On"};
 const char* no_motion[]={"","Panel","Panel","Off"};
 const uint8_t counts[]={1,2,2,2},classic_index[]={0,0,0,1},panel_index[]={0,1,1,1},off_index[]={0,1,1,0};
 panel_choices(c,dir,"12-settings-classic",labels,classic,classic_index,counts,4,1,0);
 panel_choices(c,dir,"13-settings-panel",labels,panel,panel_index,counts,4,1,0);
 panel_choices(c,dir,"14-settings-motion-off",labels,no_motion,off_index,counts,4,3,0);
 const char* held[]={"Left short","Left hold","Right short","Right hold","OK hold"};
 const char* values[]={"Prev","","Next","",""};const uint8_t one[]={1,1,1,1,1},zero[]={0,0,0,0,0};
 panel_choices(c,dir,"15-settings-held",held,values,zero,one,5,1,0);
 panel_choices(c,dir,"16-settings-held-bottom",held,values,zero,one,5,4,1);
}
'''.replace("LABELS", ",".join(json.dumps(label) for label in labels)))
    return chunks


def build_source():
    support = SUPPORT.replace(
        "int offset_x,offset_y;", "int offset_x,offset_y;size_t width,height;"
    ).replace("return 128;", "return c->width;").replace("return 64;", "return c->height;")
    support = support.replace("x,y,i->width", "x+c->offset_x,y+c->offset_y,i->width")
    support = support.replace("#ifndef __APPLE__", "#if 1 /* portable host strlcpy */")
    chunks = [support, r'''
static unsigned critical_depth,text_calls;
static bool bounds_check;
static int minimum_text_y=20;
#define FURI_CRITICAL_ENTER() ++critical_depth
#define FURI_CRITICAL_EXIT() --critical_depth
static size_t icon_get_width(const Icon*i){return i->width;}
static size_t icon_get_height(const Icon*i){return i->height;}
static void host_draw_utf8(u8g2_t*fb,int x,int y,const char*s){
 assert(!critical_depth);text_calls++;
 if(bounds_check){assert(x>=0&&x+u8g2_GetUTF8Width(fb,s)<=128);assert(y>=minimum_text_y&&y<=61);}
 u8g2_DrawUTF8(fb,x,y,s);
}
''', '#include "' + str(ROOT / "applications/services/desktop/desktop_panel_i.h") + '"']
    canvas = (ROOT / "applications/services/gui/canvas.c").read_text()
    chunks.append(canvas[canvas.index("const CanvasFontParameters canvas_font_params"):canvas.index("Canvas* canvas_init")])
    for signature in ("void canvas_clear(", "void canvas_frame_set(", "void canvas_set_color(",
                      "void canvas_set_font(", "void canvas_invert_color(",
                      "void canvas_draw_str(", "void canvas_draw_str_aligned(",
                      "uint16_t canvas_string_width(", "void canvas_draw_box(",
                      "void canvas_draw_rframe(", "void canvas_draw_line(", "void canvas_draw_rbox(",
                      "void canvas_draw_dot(", "void canvas_draw_glyph(", "size_t canvas_glyph_width(",
                      "const CanvasFontParameters* canvas_get_font_params("):
        chunks.append(function(canvas, signature).replace("u8g2_DrawUTF8", "host_draw_utf8"))
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    chunks.append(function(elements, "void elements_button_center("))
    chunks.extend(settings_chunks(elements))
    if SOURCE.exists():
        draw = re.sub(r"^#include .*\n", "", SOURCE.read_text(), flags=re.MULTILINE)
        chunks.append(f'\n#line 1 "{SOURCE}"\n' + draw)
    else:
        chunks.append("extern void desktop_panel_draw(Canvas*,void*);")
    chunks.append(DRIVER)
    assets = []
    for symbol in sorted(set(re.findall(r"\b[IA]_[A-Za-z0-9_]+", "\n".join(chunks)))):
        from PIL import Image
        candidates = list((ROOT / "assets/icons").rglob(symbol[2:] + ".png"))
        if not candidates:
            candidates = list((ROOT / "assets/icons").rglob(symbol[2:] + "/frame_01.png"))
        if not candidates:
            raise RuntimeError("Missing stock icon: " + symbol)
        im = Image.open(candidates[0]).convert("1")
        width, height = im.size
        data = [sum(1 << bit for bit in range(8)
                    if xb * 8 + bit < width and im.getpixel((xb * 8 + bit, y)) == 0)
                for y in range(height) for xb in range((width + 7) // 8)]
        assets += [f"static const uint8_t {symbol}_data[]={{" + ",".join(map(str, data)) + "};",
                   f"static const Icon {symbol}={{{width},{height},{symbol}_data}};"]
    chunks.insert(1, "\n".join(assets))
    return "\n".join(chunks)


def render(output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = build_source()
    with tempfile.TemporaryDirectory(prefix="local-panel-render-") as directory:
        work = Path(directory)
        (work / "render.c").write_text(source)
        u8g2 = sorted(p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c")
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if os.environ.get("TUMO_PANEL_RENDER_COVERAGE") else []
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-Wno-unused-function", "-Wno-unused-parameter",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-ffunction-sections",
                        *flags, gc, "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                        str(work / "render.c"), *map(str, u8g2), "-lm", "-o", str(work / "render")],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(work / "render"), str(output)], check=True, capture_output=True, text=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0" if sys.platform == "darwin" else "detect_leaks=1",
                            "LLVM_PROFILE_FILE": str(work / "render.profraw")})
        if flags:
            profile = work / "render.profdata"
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(work / "render.profraw"),
                            "-o", str(profile)], check=True, capture_output=True)
            result = subprocess.check_output(["xcrun", "llvm-cov", "export", str(work / "render"),
                                              "-instr-profile=" + str(profile)], text=True)
            (output / "coverage.json").write_text(result)
    paths = sorted(output.glob("*.pgm"))
    from PIL import Image, ImageDraw
    sheet = Image.new("RGB", (3 * 536, ((len(paths) + 2) // 3) * 294), "#e8e8e8")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(paths):
        x, y = index % 3 * 536 + 12, index // 3 * 294 + 26
        im = Image.open(path)
        sheet.paste(im.resize((512, 256), Image.Resampling.NEAREST), (x, y))
        im.resize((768, 384), Image.Resampling.NEAREST).save(path.with_suffix(".png"))
        draw.text((x, y - 20), path.stem, fill="black")
    sheet.save(output / "all-screens.png")
    overview = Image.new("RGB", (3 * 536, 2 * 294), "#e8e8e8")
    title = ImageDraw.Draw(overview)
    selected = ("01-status", "04-idle-off", "07-last-fault", "13-settings-panel",
                "14-settings-motion-off", "15-settings-held")
    for index, name in enumerate(selected):
        x, y = index % 3 * 536 + 12, index // 3 * 294 + 26
        overview.paste(Image.open(output / (name + ".pgm")).resize((512, 256), Image.Resampling.NEAREST), (x, y))
        title.text((x, y - 20), name, fill="black")
    overview.save(output / "overview.png")
    (output / "evidence.json").write_text(json.dumps({
        "renderer": "actual C + repository fonts/stock PNG assets", "sanitizers": ["address", "undefined"],
        "hardware_tested": False, "generated_c_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "frames": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    }, indent=2) + "\n")
    return paths


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    print(render(parser.parse_args().output))
