"""Native firmware-font previews and bounds checks, not LCD acceptance."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as native

ROOT = Path(__file__).resolve().parents[2]


def build_source():
    base = native.build_source(None).split("static int changes;", 1)[0]
    # Intercept the final bitmap-font operation, including aligned popup text.
    base = base.replace("#ifndef __APPLE__", r'''
static u8g2_uint_t checked_ui_text(u8g2_t* fb,u8g2_uint_t x,u8g2_uint_t y,const char* text){
 if(text && *text){assert((int)x+(int)u8g2_GetUTF8Width(fb,text)<=128);
  assert((int)y-u8g2_GetAscent(fb)>=0 && (int)y-u8g2_GetDescent(fb)<=64);}
 return u8g2_DrawUTF8(fb,x,y,text);
}
#define u8g2_DrawUTF8 checked_ui_text
#ifndef __APPLE__
''', 1)
    base = base.replace("{u8g2_DrawXBMP(&c->fb", "{assert(x>=0&&y>=0&&x+i->width<=128&&y+i->height<=64);u8g2_DrawXBMP(&c->fb")
    popup = (ROOT / "applications/services/gui/modules/popup.c").read_text()
    write = (ROOT / "applications/main/lfrfid/scenes/lfrfid_scene_write.c").read_text()
    read = (ROOT / "applications/main/lfrfid/views/lfrfid_view_read.c").read_text()
    button = (ROOT / "applications/main/ibutton/scenes/ibutton_scene_write_success.c").read_text()
    code = r'''
typedef void View;
#define furi_string_cat_str furi_string_cat
typedef struct {const Icon* frame;} IconAnimation;
static void canvas_draw_icon_animation(Canvas* c,int x,int y,IconAnimation* a){canvas_draw_icon(c,x,y,a->frame);}
typedef struct {const char* text;uint8_t x,y;Align horizontal,vertical;} TextElement;
typedef struct {uint8_t x,y;const Icon* icon;} IconElement;
typedef struct {TextElement header,text;IconElement icon;uint8_t text_width;} PopupModel;
typedef struct {void* view;void* context;void* callback;bool timer_enabled;uint32_t timer_period_in_ms;} Popup;
static void popup_set_text(Popup* p,const char* t,uint8_t x,uint8_t y,Align h,Align v){PopupModel* m=p->view;m->text=(TextElement){t,x,y,h,v};}
static void popup_set_header(Popup* p,const char* t,uint8_t x,uint8_t y,Align h,Align v){PopupModel* m=p->view;m->header=(TextElement){t,x,y,h,v};}
static void popup_set_icon(Popup* p,uint8_t x,uint8_t y,const Icon* i){((PopupModel*)p->view)->icon=(IconElement){x,y,i};}
'''
    if "static FuriString* popup_fit_text(" in popup:
        code += native.function(popup, "static FuriString* popup_fit_text(") + "\n"
    if "void popup_set_text_width(" in popup:
        code += native.function(popup, "void popup_set_text_width(") + "\n"
    else:
        code += "static void popup_set_text_width(Popup* p,uint8_t w){((PopupModel*)p->view)->text_width=w;}\n"
    code += native.function(popup, "void popup_reset(") + "\n"
    code += native.function(popup, "static void popup_view_draw_callback(") + "\n"
    code += r'''
#define LFRFID_TEXT_STORE_SIZE 128
typedef struct {void* dict;int protocol_id;FuriString* file_name;char text_store[128];Popup* popup;} LfRfid;
static const char* protocol_dict_get_name(void* dict,int protocol){(void)protocol;return dict;}
'''
    code += native.function((ROOT / "lib/toolbox/path.c").read_text(), "bool path_contains_only_ascii(") + "\n"
    code += native.function(write, "static void lfrfid_scene_write_set_status(") + "\n"
    code += (ROOT / "applications/main/lfrfid/views/lfrfid_view_read.h").read_text().replace("#pragma once", "").replace("#include <gui/view.h>", "")
    code += "typedef struct {IconAnimation* icon;LfRfidReadViewMode read_mode;} LfRfidReadViewModel;\n"
    code += native.function(read, "static void lfrfid_view_read_draw_callback(") + "\n"
    code += r'''
#define IBUTTON_TEXT_STORE_SIZE 128
enum {iButtonCustomEventBack,iButtonViewPopup,iButtonNotificationMessageSuccess,iButtonNotificationMessageGreenOn,iButtonNotificationMessageGreenOff};
typedef struct {Popup* popup;void* worker;void* view_dispatcher;char text_store[128];} iButton;
static const char* ibutton_worker_get_write_chip_name(void* w){return w;}
static void view_dispatcher_send_custom_event(void* v,int e){(void)v;(void)e;}
static void view_dispatcher_switch_to_view(void* v,int e){(void)v;(void)e;}
static void ibutton_notification_message(iButton* b,int m){(void)b;(void)m;}
static void popup_set_callback(Popup* p,void(*cb)(void*)){(void)p;(void)cb;}
static void popup_set_context(Popup* p,void* c){(void)p;(void)c;}
static void popup_set_timeout(Popup* p,uint32_t t){(void)p;(void)t;}
static void popup_enable_timeout(Popup* p){(void)p;}
'''
    code += native.function(button, "static void ibutton_scene_write_success_popup_callback(") + "\n"
    code += native.function(button, "void ibutton_scene_write_success_on_enter(") + "\n"
    code += native.function(native.DRIVER, "static void save(") + "\n"
    header = re.search(r'popup_set_header\(popup, "Writing", (\d+), (\d+),', write).groups()
    code += r'''
int main(int argc,char** argv){assert(argc==2);Canvas c={0};static uint8_t buffer[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buffer,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 PopupModel model={0};Popup popup={.view=&model};
 popup_set_text_width(&popup,255);assert(model.text_width==128);popup_reset(&popup);
 assert(model.text_width==0);
 CHECK_FIT
 LfRfid app={.dict="PAC/Stanley",.file_name=furi_string_alloc(),.popup=&popup};
 const char* names[]={"","FrontDoor","WWWWWWWWWWWWWWWWWWWWWWWWWW","Мой_домофон_очень_длинное_имя"};
 for(unsigned n=0;n<4;n++){
  popup_reset(&popup);popup_set_text_width(&popup,64);
  popup_set_icon(&popup,0,8,&I_NFC_manual_60x50);
  popup_set_header(&popup,"Writing",HEADER_X,HEADER_Y,AlignCenter,AlignTop);
  furi_string_set(app.file_name,names[n]);lfrfid_scene_write_set_status(&app,"EM4305");
  const char* original=model.text.text;char copy[128];snprintf(copy,sizeof(copy),"%s",original);
  popup_view_draw_callback(&c,&model);assert(model.text.text==original&&!strcmp(copy,original));
  assert(!strcmp(furi_string_get_cstr(app.file_name),names[n]));
  char frame[40];snprintf(frame,sizeof(frame),"%02u-write",n+1);save(&c,argv[1],frame);
 }
 popup_reset(&popup);assert(model.text_width==0);
 iButton button={.popup=&popup,.worker="TM2004"};ibutton_scene_write_success_on_enter(&button);
 popup_view_draw_callback(&c,&model);save(&c,argv[1],"05-ibutton-chip");
 popup_reset(&popup);button.worker="";ibutton_scene_write_success_on_enter(&button);
 popup_view_draw_callback(&c,&model);save(&c,argv[1],"06-ibutton-success");
 IconAnimation animation={.frame=&I_RFID_Spinner};LfRfidReadViewModel read={.icon=&animation};
 for(unsigned n=0;n<6;n++){read.read_mode=n;canvas_clear(&c);lfrfid_view_read_draw_callback(&c,&read);
  char frame[40];snprintf(frame,sizeof(frame),"%02u-read",n+7);save(&c,argv[1],frame);}
 furi_string_free(app.file_name);return 0;}
'''
    code = code.replace("HEADER_X", header[0]).replace("HEADER_Y", header[1])
    code = code.replace("CHECK_FIT", r'''
 canvas_set_font(&c,FontSecondary);
 FuriString* tiny=popup_fit_text(&c,"Wide text",1);assert(furi_string_empty(tiny));furi_string_free(tiny);
 FuriString* unicode=popup_fit_text(&c,"WWWWWWWWWWключ_длинный",64);
 assert(canvas_string_width(&c,furi_string_get_cstr(unicode))<=64);
 printf("%s\n",furi_string_get_cstr(unicode));furi_string_free(unicode);
''' if "static FuriString* popup_fit_text(" in popup else "")
    assets = []
    symbols = set(re.findall(r"\bI_[A-Za-z0-9_]+", code))
    for symbol in sorted(symbols):
        if re.search(r"\bIcon " + re.escape(symbol) + r"=", base):
            continue
        if symbol == "I_RFID_Spinner":
            candidates = sorted((ROOT / "assets/icons").rglob("Round_loader_8x8/*"))
            candidates = [p for p in candidates if p.suffix == ".png"]
        else:
            candidates = list((ROOT / "assets/icons").rglob(symbol[2:] + ".png"))
        if not candidates:
            raise RuntimeError("Missing native icon " + symbol)
        frame = Image.open(candidates[0]).convert("1"); w, h = frame.size
        bits = [sum(1 << bit for bit in range(8) if x*8+bit < w and not frame.getpixel((x*8+bit,y)))
                for y in range(h) for x in range((w+7)//8)]
        assets += [f"static const uint8_t {symbol}_data[]={{"+",".join(map(str,bits))+"};",
                   f"static const Icon {symbol}={{{w},{h},{symbol}_data}};"]
    return base + "\n" + "\n".join(assets) + "\n" + code


def render(output):
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rfid-ui-render-") as directory:
        work = Path(directory); (work / "render.c").write_text(build_source())
        sources = [str(p) for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        result = subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
                                 "-fno-sanitize-recover=all", "-ffunction-sections", gc,
                                 "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                                 str(work / "render.c"), *sources, "-lm", "-o", str(work / "render")],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "render"), str(output.resolve())], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
    frames = sorted(output.glob("*.pgm")); assert len(frames) == 12
    sheet = Image.new("RGB", (1212, 932), "#ececec"); labels = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        with Image.open(path) as frame:
            frame.save(path.with_suffix(".png")); x, y = (index%3)*404+10, (index//3)*232+28
            sheet.paste(frame.resize((384,192), Image.Resampling.NEAREST).convert("RGB"),(x,y))
            labels.text((x,y-20),path.stem,fill="black")
    sheet.save(output / "contact-sheet.png")
    return output / "contact-sheet.png"


if __name__ == "__main__":
    parser=argparse.ArgumentParser();parser.add_argument("output",type=Path)
    print(render(parser.parse_args().output))
