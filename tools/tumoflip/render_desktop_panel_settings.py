"""Render the real Desktop Settings list builder and stock widgets on fake state."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as gui

ROOT = Path(__file__).resolve().parents[2]


def source():
    text = gui.build_source(None, skip_text_input=True).split("int main(int argc,char** argv)", 1)[0]
    stock = (ROOT / "applications/services/gui/modules/variable_item_list.c").read_text()
    signature = "static void variable_item_list_draw_callback("
    text = text.replace(gui.function(text, signature), gui.function(stock, signature), 1)
    start = (ROOT / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c").read_text()
    text += '\n#include "applications/services/desktop/desktop_panel_i.h"\n'
    text += "\n" + re.search(r"typedef enum \{.*?\} DesktopSettingsEntry;", start, re.S).group() + "\n"
    text += "\n".join(re.findall(r"^#define \w+ \d+$", start, re.M)) + "\n"
    text += "\n".join(re.findall(r"(?:static )?const (?:char\* const|uint32_t) \w+\[[^\]]*\]\s*=\s*\{.*?\};", start, re.S)) + "\n"
    text += r'''
typedef struct {DesktopSettings settings;DesktopPanelSettings panel;DesktopMode shortcut_profile;
 bool supports_panel;uint8_t menu_idx;VariableItemList*variable_item_list;void*view_dispatcher;} DesktopSettingsApp;
enum {MenuStyleCount=6,DesktopSettingsAppViewVarItemList};
static unsigned menu_style_load(void){return 0;}
static const char* menu_style_get_name(unsigned i){(void)i;return "List";}
static unsigned value_index_uint32(unsigned value,const uint32_t*values,unsigned count){for(unsigned i=0;i<count;i++)if(values[i]==value)return i;return 0;}
static VariableItem*variable_item_list_add(VariableItemList*list,const char*label,unsigned count,void(*cb)(VariableItem*),void*ctx){
 (void)cb;(void)ctx;VariableItemListModel*m=list->view;
 VariableItem item={.label=label,.current_value_text=furi_string_alloc(),.values_count=count};
 VariableItemArray_push_back(m->items,item);return VariableItemArray_back(m->items);
}
static void variable_item_set_current_value_index(VariableItem*i,unsigned n){i->current_value_index=n;}
static void variable_item_set_current_value_text(VariableItem*i,const char*s){furi_string_set_str(i->current_value_text,s);}
static void variable_item_list_set_enter_callback(VariableItemList*l,void(*cb)(void*,uint32_t),void*c){assert(l&&cb&&c);}
static void variable_item_list_set_selected_item(VariableItemList*l,unsigned n){((VariableItemListModel*)l->view)->position=n;}
static void view_dispatcher_switch_to_view(void*d,unsigned n){(void)n;assert(d);}
static void desktop_settings_scene_start_var_list_enter_callback(void*c,uint32_t n){(void)c;(void)n;}
'''
    for name in re.findall(r"static void (desktop_settings_scene_start_\w+)\(VariableItem\* item\)", start):
        text += f"\nstatic void {name}(VariableItem*i){{(void)i;}}\n"
    text += gui.function(start, "void desktop_settings_scene_start_on_enter(")
    text += r'''
static void reset_list(VariableItemListModel*m){
 for(unsigned i=0;i<VariableItemArray_size(m->items);i++)furi_string_free(VariableItemArray_get(m->items,i)->current_value_text);
 VariableItemArray_reset(m->items);m->position=m->window_position=0;
}
int main(int argc,char**argv){assert(argc==2);Canvas c={0};uint8_t pixels[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,pixels,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&c,FontSecondary);
 for(unsigned i=0;i<2;i++)assert(canvas_string_width(&c,desktop_mode_text[i])<=37);
 VariableItemListModel model={0};VariableItemArray_init(model.items);VariableItemList list={.view=&model};
 DesktopSettingsApp app={.settings={.auto_lock_delay_ms=60000,.display_clock=1},.supports_panel=true,
 .variable_item_list=&list,.view_dispatcher=&model,.menu_idx=1};
 desktop_panel_defaults(&app.panel,&app.settings);
 desktop_settings_scene_start_on_enter(&app);assert(VariableItemArray_size(model.items)==17);
 assert(!strcmp(VariableItemArray_get(model.items,1)->label,"Desktop view"));
 canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"01-wallpaper");
 reset_list(&model);app.panel.mode=app.shortcut_profile=DesktopModeDashboard;app.menu_idx=2;
 desktop_settings_scene_start_on_enter(&app);canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"02-dashboard");
 model.position=13;model.window_position=12;canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"03-panel-left");
 model.position=16;model.window_position=13;canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"04-panel-right-ok");
 assert(!strcmp(VariableItemArray_get(model.items,12)->label,"Left short"));
 assert(!strcmp(furi_string_get_cstr(VariableItemArray_get(model.items,12)->current_value_text),"Prev"));
 for(unsigned i=12;i<17;i++)assert(canvas_string_width(&c,furi_string_get_cstr(VariableItemArray_get(model.items,i)->current_value_text))<=37);
 reset_list(&model);app.shortcut_profile=DesktopModeWallpaper;app.menu_idx=13;
 desktop_settings_scene_start_on_enter(&app);model.window_position=12;canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"05-wallpaper-bindings");
 reset_list(&model);app.supports_panel=false;app.panel.mode=0;app.menu_idx=1;
 desktop_settings_scene_start_on_enter(&app);assert(VariableItemArray_size(model.items)==14);
 assert(!strcmp(VariableItemArray_get(model.items,1)->label,"Auto Lock Time"));
 canvas_clear(&c);variable_item_list_draw_callback(&c,&model);save(&c,argv[1],"06-older-core");
 reset_list(&model);VariableItemArray_clear(model.items);
 return 0;
}
'''
    return text


def render(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="panel-settings-ui-") as directory:
        work=Path(directory);(work/"render.c").write_text(source())
        library=[path for path in (ROOT/"lib/u8g2").glob("*.c") if path.name!="u8g2_glue.c"]
        built=subprocess.run(["cc","-std=c11","-O1","-g","-w","-fsanitize=address,undefined",
            "-ffunction-sections","-Wl,-dead_strip" if sys.platform=="darwin" else "-Wl,--gc-sections",
            "-I",str(ROOT),"-I",str(ROOT/"lib/u8g2"),"-I",str(ROOT/"lib/mlib"),
            str(work/"render.c"),*map(str,library),"-lm","-o",str(work/"render")],capture_output=True,text=True)
        if built.returncode:raise RuntimeError(built.stderr)
        subprocess.run([str(work/"render"),str(output.resolve())],check=True,timeout=15)
    frames=sorted(output.glob("*.pgm"));assert len(frames)==6
    sheet=Image.new("RGB",(548,3*164),"#eee");draw=ImageDraw.Draw(sheet)
    for i,frame in enumerate(frames):
        x,y=8+(i%2)*274,22+(i//2)*164;draw.text((x,y-18),frame.stem,fill="black")
        sheet.paste(Image.open(frame).convert("RGB").resize((256,128),Image.Resampling.NEAREST),(x,y))
    sheet.save(output/"desktop-settings.png")
    print("6 production Settings layouts rendered; old-core capability fallback verified")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("output",type=Path)
    render(parser.parse_args().output)
