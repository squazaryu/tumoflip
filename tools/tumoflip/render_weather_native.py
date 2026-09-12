"""Render production Weather/GUI draw callbacks using the firmware's own U8g2.

The host adapters supply display models and strings, not replacement layouts.
Radio, input handling and physical LCD timing are outside this renderer.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import os
import json
import hashlib
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    while ";" in source[start:source.index("{",start)]:
        start=source.index(signature,start+len(signature))
    return source[start:source.index("\n}", start) + 2]


def read(path, ref=None):
    if ref:
        return subprocess.check_output(["git", "show", f"{ref}:{path}"], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


SUPPORT = r'''
#include "u8g2.h"
#include "m-array.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#define furi_check assert
#define furi_assert assert
#define furi_crash() abort()
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(x,hi,lo) MIN(MAX(x,lo),hi)
#define COUNT_OF(x) (sizeof(x)/sizeof((x)[0]))
typedef enum {ColorWhite=0,ColorBlack=1,ColorXOR=2} Color;
typedef enum {FontPrimary,FontSecondary,FontKeyboard,FontBigNumbers,FontBatteryPercent} Font;
#define FontTotalNumber 5
typedef struct {size_t leading_default,leading_min,height,descender;} CanvasFontParameters;
typedef enum {AlignLeft,AlignRight,AlignTop,AlignBottom,AlignCenter} Align;
typedef struct {u8g2_t fb; int offset_x,offset_y;} Canvas;
typedef struct {char data[2048];} FuriString;
static FuriString* furi_string_alloc(void) {return calloc(1,sizeof(FuriString));}
static FuriString* furi_string_alloc_printf(const char* fmt,...) {FuriString* s=furi_string_alloc();va_list ap;va_start(ap,fmt);vsnprintf(s->data,sizeof(s->data),fmt,ap);va_end(ap);return s;}
static const char* furi_string_get_cstr(const FuriString* s) {return s->data;}
static void furi_string_set_str(FuriString* d,const char* s) {snprintf(d->data,sizeof(d->data),"%s",s);}
static void furi_string_copy(FuriString* d,const FuriString* s) {furi_string_set_str(d,s->data);}
#define furi_string_set(d,s) _Generic((s),FuriString*:furi_string_copy,const FuriString*:furi_string_copy,default:furi_string_set_str)(d,s)
static FuriString* furi_string_alloc_set_str(const char* s) {FuriString* d=furi_string_alloc();furi_string_set_str(d,s);return d;}
static FuriString* furi_string_alloc_copy(const FuriString* s) {return furi_string_alloc_set_str(s->data);}
#define furi_string_alloc_set(s) _Generic((s),FuriString*:furi_string_alloc_copy,const FuriString*:furi_string_alloc_copy,default:furi_string_alloc_set_str)(s)
static size_t furi_string_size(const FuriString* s) {return strlen(s->data);}
static bool furi_string_empty(const FuriString* s) {return !s->data[0];}
static void furi_string_reset(FuriString* s) {s->data[0]=0;}
static void furi_string_free(FuriString* s) {free(s);}
static void furi_string_left(FuriString* s,size_t n) {if(n<strlen(s->data))s->data[n]=0;}
static void furi_string_right(FuriString* s,size_t n) {n=MIN(n,strlen(s->data));memmove(s->data,s->data+n,strlen(s->data+n)+1);}
static char furi_string_get_char(FuriString* s,size_t i) {return s->data[i];}
static void furi_string_cat(FuriString* s,const char* t) {strncat(s->data,t,sizeof(s->data)-strlen(s->data)-1);}
static void furi_string_set_strn(FuriString* s,const char* t,size_t n) {assert(n<sizeof(s->data));memcpy(s->data,t,n);s->data[n]=0;}
static void furi_string_push_back(FuriString* s,char c) {size_t n=strlen(s->data);assert(n+1<sizeof(s->data));s->data[n]=c;s->data[n+1]=0;}
static void furi_string_printf(FuriString* s,const char* fmt,...) {va_list ap;va_start(ap,fmt);vsnprintf(s->data,sizeof(s->data),fmt,ap);va_end(ap);}
static void furi_string_cat_printf(FuriString* s,const char* fmt,...) {size_t n=strlen(s->data);va_list ap;va_start(ap,fmt);vsnprintf(s->data+n,sizeof(s->data)-n,fmt,ap);va_end(ap);}
#define FuriWaitForever 0
static void furi_mutex_acquire(void* m,int t) {(void)m;(void)t;}
static void furi_mutex_release(void* m) {(void)m;}
static int furi_kernel_get_tick_frequency(void) {return 1000;}
static void furi_timer_start(void* t,int frequency) {(void)t;(void)frequency;}
#define with_view_model(view,decl,body,update) do {decl=(view);body;} while(0)
#define StrintParseNoError 0
static int strint_to_int64(const char* s,void* e,int64_t* value,int base) {(void)e;char* end;errno=0;*value=strtoll(s,&end,base);return errno || *end || end==s;}
typedef void (*NumberInputCallback)(int32_t,void*);
typedef void (*TextInputCallback)(void*);
typedef bool (*TextInputValidatorCallback)(const char*,FuriString*,void*);
typedef struct {size_t width,height;const uint8_t* data;} Icon;
static size_t canvas_width(const Canvas* c) {(void)c;return 128;}
static size_t canvas_height(const Canvas* c) {(void)c;return 64;}
static size_t canvas_current_font_height(const Canvas* c) {return u8g2_GetMaxCharHeight(&c->fb)+(c->fb.font==u8g2_font_haxrcorp4089_tr?1:0);}
static void canvas_draw_icon(Canvas* c,int x,int y,const Icon* i) {u8g2_DrawXBMP(&c->fb,x,y,i->width,i->height,i->data);}
static void canvas_set_bitmap_mode(Canvas* c,bool mode) {u8g2_SetBitmapMode(&c->fb,mode);}
static bool float_is_equal(float a,float b) {return fabsf(a-b)<0.0001f;}
static float locale_celsius_to_fahrenheit(float c) {return c*1.8f+32.0f;}
'''


MODELS = r'''
typedef struct WidgetElement {void* model;void* model_mutex;} WidgetElement;
typedef struct VariableItem {const char* label;uint8_t current_value_index;FuriString* current_value_text;
    uint8_t values_count;void (*change_callback)(struct VariableItem*);bool locked;FuriString* locked_message;void* context;} VariableItem;
ARRAY_DEF(VariableItemArray,VariableItem,M_POD_OPLIST)
typedef struct {VariableItemArray_t items;uint8_t position,window_position;size_t scroll_counter;bool locked_message_visible;} VariableItemListModel;
typedef struct {void* view;void* locked_timer;} VariableItemList;
typedef struct {FuriString* label;size_t index;void* callback;void* callback_context;bool has_extended_events,locked;FuriString* locked_message;} SubmenuItem;
ARRAY_DEF(SubmenuItemArray,SubmenuItem,M_POD_OPLIST)
typedef struct {SubmenuItemArray_t items;FuriString* header;size_t position,window_position;bool locked_message_visible,is_vertical;} SubmenuModel;
typedef struct {FuriString* item_str;uint8_t type;} WSReceiverMenuItem;
ARRAY_DEF(WSReceiverMenuItemArray,WSReceiverMenuItem,M_POD_OPLIST)
typedef struct {WSReceiverMenuItemArray_t data;} WSReceiverHistory;
enum {SubGhzProtocolTypeUnknown,SubGhzProtocolTypeStatic,SubGhzProtocolTypeDynamic,SubGhzProtocolWeatherStation};
enum {WSReceiverBarShowDefault,WSReceiverBarShowLock,WSReceiverBarShowToUnlockPress,WSReceiverBarShowUnlock};
typedef struct {FuriString* frequency_str;FuriString* preset_str;FuriString* history_stat_str;
    WSReceiverHistory* history;uint16_t idx,list_offset,history_item;int bar_show;uint8_t u_rssi;bool external_radio;} WSReceiverModel;
typedef struct {uint32_t id,timestamp;uint8_t data_count_bit,channel,btn,battery_low,humidity;uint64_t data;float temp;} WSBlockGeneric;
typedef struct {uint32_t curr_ts;FuriString* protocol_name;WSBlockGeneric* generic;bool display_fahrenheit;} WSReceiverInfoModel;
#define WS_NO_CHANNEL 255
#define WS_NO_ID 0xFFFFFFFF
#define WS_NO_BTN 255
#define WS_NO_BATT 255
#define WS_NO_HUMIDITY 255
#define WS_NO_TEMPERATURE -273.0f
typedef struct {int32_t x,y,leading_min,leading_default;size_t height,descender,len;const char* text;} ElementTextBoxLine;
#define FRAME_HEIGHT 12
#define MAX_LEN_PX 112
#define MENU_ITEMS 4u
'''


DRIVER = r'''
static int changes;
static void changed(VariableItem* i) {(void)i;changes++;}
static void check_navigation(void) {
    VariableItemListModel model={0};VariableItemArray_init(model.items);
    for(int i=0;i<17;i++){VariableItem item={.values_count=i==0?1:3,.change_callback=changed};VariableItemArray_push_back(model.items,item);}
    VariableItemList list={.view=&model};
    variable_item_list_process_right(&list);assert(changes==0);
    variable_item_list_process_down(&list);assert(model.position==1);
    for(int i=0;i<4;i++)variable_item_list_process_right(&list);
    assert(VariableItemArray_get(model.items,1)->current_value_index==2 && changes==2);
    for(int i=0;i<4;i++)variable_item_list_process_left(&list);
    assert(VariableItemArray_get(model.items,1)->current_value_index==0 && changes==4);
    for(int i=0;i<1000;i++) {
        variable_item_list_process_down(&list);
        assert(model.position<17 && model.window_position<=13);
        assert(model.position>=model.window_position && model.position-model.window_position<4);
    }
    for(int i=0;i<1000;i++) {
        variable_item_list_process_up(&list);
        assert(model.position<17 && model.window_position<=13);
        assert(model.position>=model.window_position && model.position-model.window_position<4);
    }
    VariableItemArray_clear(model.items);
}
static void save(Canvas* c,const char* directory,const char* name) {
    char path[2048];snprintf(path,sizeof(path),"%s/%s.pgm",directory,name);
    FILE* f=fopen(path,"wb");assert(f);fprintf(f,"P5\n128 64\n255\n");
    for(int y=0;y<64;y++)for(int x=0;x<128;x++) {
        uint8_t p=(c->fb.tile_buf_ptr[(y/8)*128+x]>>(y%8)&1)?0:255;fwrite(&p,1,1,f);
    } fclose(f);
}
static void menu(Canvas* c,const char* dir,const char* name,const char* header,const char** labels,int n,int selected,int window) {
    SubmenuModel m={0};SubmenuItemArray_init(m.items);m.header=furi_string_alloc_set(header);m.position=selected;m.window_position=window;
    for(int i=0;i<n;i++){SubmenuItem item={.label=furi_string_alloc_set(labels[i])};SubmenuItemArray_push_back(m.items,item);}
    submenu_view_draw_callback(c,&m);save(c,dir,name);
    for(int i=0;i<n;i++)furi_string_free(SubmenuItemArray_get(m.items,i)->label);
    SubmenuItemArray_clear(m.items);furi_string_free(m.header);
}
static void choices(Canvas* c,const char* dir,const char* name,const char** labels,const char** values,int n,int selected,int window) {
    VariableItemListModel m={0};VariableItemArray_init(m.items);m.position=selected;m.window_position=window;
    for(int i=0;i<n;i++){
        bool editable=!strcmp(labels[i],"Minus")||!strcmp(labels[i],"Battery")||!strcmp(labels[i],"Button")||!strcmp(labels[i],"Channel mode")||!strcmp(labels[i],"Auto TX")||!strcmp(labels[i],"Temp. unit")||strchr(labels[i],':');
        VariableItem item={.label=labels[i],.current_value_text=furi_string_alloc_set(values[i]),.values_count=editable?2:1};VariableItemArray_push_back(m.items,item);
    }
    variable_item_list_draw_callback(c,&m);save(c,dir,name);
    for(int i=0;i<n;i++)furi_string_free(VariableItemArray_get(m.items,i)->current_value_text);
    VariableItemArray_clear(m.items);
}
static void text_scroll(Canvas* c,int x,int y,int width,int height,const char* text,int position) {
    WidgetElementTextScrollModel model={.x=x,.y=y,.width=width-4,.height=height,.text=furi_string_alloc_set(text),.scroll_pos_total=1,.scroll_pos_current=position};
    TextScrollLineArray_init(model.line_array);WidgetElement e={.model=&model};widget_element_text_scroll_draw(c,&e);
    for(size_t i=0;i<TextScrollLineArray_size(model.line_array);i++)furi_string_free(TextScrollLineArray_get(model.line_array,i)->text);
    TextScrollLineArray_clear(model.line_array);furi_string_free(model.text);
}
int main(int argc,char** argv) {
    assert(argc==2);check_navigation();Canvas c={0};static uint8_t buf[1024];
    static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
    c.fb.u8x8.display_info=&info;
    u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
    canvas_set_font(&c,FontSecondary);canvas_set_color(&c,ColorBlack);
    const char* menu_labels[]={"Receiver","Load saved","Settings","Simulation","Info"};
    menu(&c,argv[1],"01-menu","Weather Editor",menu_labels,5,0,0);
    menu(&c,argv[1],"02-menu-bottom","Weather Editor",menu_labels,5,4,2);
    const char* labels[]={"Protocol","Sensor ID","Frame bits","TX frequency","Save RX","Send RX data","Temperature","Minus","Humidity","Battery","Button","Channel mode","Channel","Save edit","Send edit","Auto TX interval","Auto TX"};
    const char* values[]={"Acurite 592TXR","0x00001234","56","433.920 MHz","","","+21.4 C","NO","58 %","OK","NO","AUTO","RX 2","","","30 s","NO"};
    choices(&c,argv[1],"03-editor-head",labels,values,17,0,0);
    choices(&c,argv[1],"04-editor-values",labels,values,17,6,6);
    choices(&c,argv[1],"05-editor-tail",labels,values,17,15,13);
    const char* cl[]={"Frequency:","Hopping:","Modulation:","Lock controls"};const char* cv[]={"433.92","OFF","AM650",""};
    choices(&c,argv[1],"06-config",cl,cv,4,1,0);
    const char* sl[]={"Temp. unit"};const char* sv[]={"C"};choices(&c,argv[1],"07-settings",sl,sv,1,0,0);
    const char* saved[]={"ORG","MOD"};menu(&c,argv[1],"08-load","Load saved",saved,2,0,0);
    const char* files[]={"2026-09-12_20-14-02.ws","2026-09-12_20-18-41.ws","2026-09-12_20-21-06.ws","2026-09-12_20-25-03.ws","Next page >"};
    menu(&c,argv[1],"09-files","ORG 1/2",files,5,0,0);
    menu(&c,argv[1],"10-files-next","ORG 1/2",files,5,4,2);
    menu(&c,argv[1],"11-empty","ORG empty",NULL,0,0,0);
    const char* protocols[]={"Acurite 592TXR","Acurite 5n1","Ambient_Weather","Bresser-3CH"};
    const char* simulation[]={"Nexus-TH","ThermoPRO-TX4","Bresser-3CH","Auriol HG06061"};
    menu(&c,argv[1],"12-simulation","Simulation",simulation,4,0,0);
    WSReceiverHistory hist;WSReceiverMenuItemArray_init(hist.data);
    WSReceiverModel rx={.frequency_str=furi_string_alloc_set("433.92"),.preset_str=furi_string_alloc_set("AM"),.history_stat_str=furi_string_alloc_set("3/50"),.history=&hist};
    ws_view_receiver_draw(&c,&rx);save(&c,argv[1],"13-scan");
    for(int i=0;i<3;i++){WSReceiverMenuItem item={furi_string_alloc_set(protocols[i]),SubGhzProtocolWeatherStation};WSReceiverMenuItemArray_push_back(hist.data,item);}
    rx.history_item=3;ws_view_receiver_draw(&c,&rx);save(&c,argv[1],"14-stations");
    WSBlockGeneric data={.id=0x2A,.timestamp=100,.data_count_bit=56,.channel=2,.btn=1,.battery_low=0,.humidity=58,.data=0x1234ABCD,.temp=21.4f};
    WSReceiverInfoModel details={.curr_ts=112,.protocol_name=furi_string_alloc_set("Acurite 592TXR"),.generic=&data};
    ws_view_receiver_info_draw(&c,&details);save(&c,argv[1],"15-details");
    furi_string_set(details.protocol_name,"LaCrosse_TX141THBv2");data.temp=-19.8;data.id=0x12345678;data.data=0x123456789ABCDEF0;
    ws_view_receiver_info_draw(&c,&details);save(&c,argv[1],"16-details-long");
    canvas_clear(&c);canvas_set_color(&c,ColorBlack);canvas_set_font(&c,FontSecondary);
    NumberInputModel input={.header=furi_string_alloc_set("Temperature x0.1 C"),.text_buffer=furi_string_alloc_set("214"),.current_number=214,.min_value=0,.max_value=9990};
    number_input_view_draw_callback(&c,&input);save(&c,argv[1],"17-number-input");
    canvas_clear(&c);canvas_set_color(&c,ColorBlack);
    elements_text_box(&c,0,2,128,14,AlignCenter,AlignCenter,"RX saved",false);
    text_scroll(&c,2,18,124,44,"/ext/apps_data/weather_editor/profiles/ORG/2026-09-12_20-14-02.ws",0);
    save(&c,argv[1],"18-result");
    canvas_clear(&c);canvas_set_color(&c,ColorBlack);
    elements_text_box(&c,0,2,128,14,AlignCenter,AlignCenter,"TX error",false);
    text_scroll(&c,2,18,124,44,"External radio disconnected. Reconnect the module and try again.",0);
    save(&c,argv[1],"19-error");
    canvas_clear(&c);canvas_set_color(&c,ColorBlack);
    elements_text_box(&c,0,0,128,14,AlignCenter,AlignBottom,"\e#\e!                                                      \e!\n",false);
    elements_text_box(&c,0,2,128,14,AlignCenter,AlignBottom,"\e#\e!         Weather Editor       \e!\n",false);
    text_scroll(&c,0,16,128,50,"\e#Information\nVersion: 2.4\nAuthor: altruista86\n\n\e#RX protocols\nAcurite 592TXR\nAcurite 5n1\n",0);
    elements_button_center(&c,"");save(&c,argv[1],"20-about");
    canvas_clear(&c);canvas_set_color(&c,ColorBlack);canvas_set_font(&c,FontSecondary);
    char hex[9]="1234";
    TextInputModel hex_input={.header="Sensor ID HEX (0-9 A-F)",.text_buffer=hex,.text_buffer_size=sizeof(hex),.minimum_length=1,.cursor_pos=4};
    text_input_view_draw_callback(&c,&hex_input);save(&c,argv[1],"21-hex-input");
    // Exercise cursor positions, empty values, full IDs and clipped long text.
    for(int n=0;n<80;n++) {
        char input_text[81];memset(input_text,'A',n);input_text[n]=0;
        for(int position=0;position<=n;position++) {
            hex_input.text_buffer=input_text;hex_input.text_buffer_size=sizeof(input_text);hex_input.cursor_pos=position;
            canvas_set_font(&c,FontSecondary);text_input_view_draw_callback(&c,&hex_input);
        }
    }
    return 0;
}
'''


def build_source(ref, skip_text_input=False):
    canvas = read("applications/services/gui/canvas.c", ref)
    elements = read("applications/services/gui/elements.c", ref)
    chunks = [SUPPORT]
    chunks.append(canvas[canvas.index("const CanvasFontParameters canvas_font_params"):canvas.index("Canvas* canvas_init")])
    chunks.append(function(canvas,"const CanvasFontParameters* canvas_get_font_params("))
    for name in ["void canvas_clear(", "void canvas_set_color(", "void canvas_invert_color(",
                 "void canvas_set_font(", "void canvas_draw_str(", "void canvas_draw_str_aligned(",
                 "uint16_t canvas_string_width(", "size_t canvas_glyph_width(",
                 "void canvas_draw_dot(", "void canvas_draw_box(", "void canvas_draw_rbox(",
                 "void canvas_draw_frame(", "void canvas_draw_rframe(", "void canvas_draw_line(",
                 "void canvas_draw_circle(", "void canvas_draw_glyph("]:
        chunks.append(function(canvas,name))
    ef = ["void elements_scrollbar_pos(", "void elements_scrollbar(",
          "void elements_slightly_rounded_box(", "void elements_slightly_rounded_frame(",
          "void elements_bold_rounded_frame(", "void elements_string_fit_width(",
          "void elements_scrollable_text_line_str(", "static size_t\n    elements_get_max_chars_to_fit(", "void elements_multiline_text_aligned(",
          "void elements_multiline_text(", "void elements_button_left(", "void elements_button_center("]
    for name in ef:
        chunks.append(function(elements,name))
    chunks.append(MODELS)
    chunks.append("\n".join(re.findall(r"^#define ELEMENTS_.*$",read("applications/services/gui/elements.h",ref),re.MULTILINE)))
    chunks.append(function(elements,"void elements_text_box("))
    number=read("applications/services/gui/modules/number_input.c",ref)
    chunks.append(number[number.index("typedef struct"):number.index("static size_t number_input_get_row_size")])
    for name in ["static size_t number_input_get_row_size(","static const NumberInputKey* number_input_get_row(","static void number_input_draw_input(","static bool number_input_use_sign(","static bool is_number_too_large(","static bool is_number_too_small(","static void number_input_view_draw_callback("]:
        chunks.append(function(number,name))
    text_input=read("applications/services/gui/modules/text_input.c",ref).replace("keyboard_","text_keyboard_")
    chunks.append(text_input[text_input.index("typedef struct"):text_input.index("static uint8_t get_row_size(")])
    for name in ["static uint8_t get_row_size(","static const TextInputKey* get_row(","static bool char_is_lowercase(","static char char_to_uppercase(","static void text_input_view_draw_callback("]:
        chunks.append(function(text_input,name))
    scroll=read("applications/services/gui/modules/widget_elements/widget_element_text_scroll.c",ref)
    chunks.append(scroll[scroll.index("#define WIDGET_ELEMENT_TEXT_SCROLL_BAR_OFFSET"):scroll.index("static bool widget_element_text_scroll_input(")])
    submenu=read("applications/services/gui/modules/submenu.c",ref)
    chunks.append(function(submenu,"static size_t submenu_items_on_screen("))
    chunks.append(function(submenu,"static void submenu_view_draw_callback("))
    local = "applications_user/weather_editor/views/weather_variable_item_list.c"
    if not ref and (ROOT/local).exists():
        draw_source=read(local).replace("WeatherVariableItem","VariableItem").replace("weather_variable_item","variable_item")
    else:
        draw_source=read("applications/services/gui/modules/variable_item_list.c",ref)
    chunks.append(function(draw_source,"static void variable_item_list_draw_callback("))
    for name in ["void variable_item_list_process_up(","void variable_item_list_process_down(","VariableItem* variable_item_list_get_selected_item(","void variable_item_list_process_left(","void variable_item_list_process_right("]:
        chunks.append(function(draw_source,name))
    receiver=read("applications_user/weather_editor/views/weather_station_receiver.c",ref)
    chunks.append(receiver[receiver.index("static const Icon* ReceiverItemIcons"):receiver.index("typedef enum",receiver.index("static const Icon* ReceiverItemIcons"))])
    for name in ["static void ws_view_receiver_draw_frame(","static void ws_view_rssi_draw(","void ws_view_receiver_draw("]:
        chunks.append(function(receiver,name))
    chunks.append(function(read("applications_user/weather_editor/views/weather_station_receiver_info.c",ref),"void ws_view_receiver_info_draw("))
    # Compile assets from the same PNGs consumed by fbt's icon generator.
    icon_names=sorted(set(re.findall(r"\bI_[A-Za-z0-9_]+", "\n".join(chunks))))
    assets=[]
    for symbol in icon_names:
        candidates=list((ROOT/"applications_user/weather_editor/images").glob(symbol[2:]+".png"))
        candidates+=list((ROOT/"assets/icons").rglob(symbol[2:]+".png"))
        if not candidates:
            raise RuntimeError("Missing icon "+symbol)
        im=Image.open(candidates[0]).convert("1"); w,h=im.size
        bits=[]
        for y in range(h):
            for xb in range((w+7)//8):
                bits.append(sum((1<<i) for i in range(8) if xb*8+i<w and im.getpixel((xb*8+i,y))==0))
        assets.append(f"static const uint8_t {symbol}_data[]={{"+",".join(map(str,bits))+"};")
        assets.append(f"static const Icon {symbol}={{{w},{h},{symbol}_data}};")
    chunks.insert(1,"\n".join(assets))
    driver=DRIVER
    actions=read("applications_user/weather_editor/scenes/weather_station_scene_actions.c",ref)
    if 'list, "TX freq."' in actions:
        driver=driver.replace('"TX frequency"','"TX freq."')
    if 'list, "TX interval"' in actions:
        driver=driver.replace('"Auto TX interval"','"TX interval"')
    about=read("applications_user/weather_editor/scenes/weather_station_scene_about.c",ref)
    scroll_geometry=re.search(r'widget_add_text_scroll_element\(app->widget, (\d+), (\d+), (\d+), (\d+)',about).groups()
    driver=driver.replace('text_scroll(&c,0,16,128,50,','text_scroll(&c,'+','.join(scroll_geometry)+',')
    if r'\e#Information' not in about:
        driver=driver.replace(r'\e#Information\nVersion:', 'Version:')
    if 'widget_add_button_element(' not in about:
        driver=driver.replace('elements_button_center(&c,"");save(&c,argv[1],"20-about");','save(&c,argv[1],"20-about");')
    if skip_text_input:
        driver=driver[:driver.index('    char hex[9]')]+"    return 0;\n}\n"
    chunks.append(driver)
    return "\n".join(chunks)


def main():
    ap=argparse.ArgumentParser();ap.add_argument("output",type=Path);ap.add_argument("--ref");ap.add_argument("--sanitize",action="store_true");ap.add_argument("--skip-text-input",action="store_true");ap.add_argument("--compare",type=Path)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    generated_source=build_source(args.ref,args.skip_text_input)
    with tempfile.TemporaryDirectory() as temp:
        temp=Path(temp);src=temp/"render.c";src.write_text(generated_source)
        sources=[p for p in (ROOT/"lib/u8g2").glob("*.c") if p.name!="u8g2_glue.c"]
        flags=["-fsanitize=address"] if args.sanitize else []
        subprocess.run(["cc","-std=c11","-O1","-g","-w",*flags,"-ffunction-sections","-Wl,-dead_strip","-I",str(ROOT/"lib/u8g2"),"-I",str(ROOT/"lib/mlib"),str(src),*[str(p) for p in sources],"-o",str(temp/"render")],check=True)
        subprocess.run([str(temp/"render"),str(args.output.resolve())],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
    for path in sorted(args.output.glob("*.pgm")):
        Image.open(path).resize((768,384),Image.Resampling.NEAREST).save(path.with_suffix(".png"))
    paths=sorted(args.output.glob("*.pgm"))
    sheet=Image.new("RGB",(4*536,((len(paths)+3)//4)*298),(230,230,230))
    draw=ImageDraw.Draw(sheet)
    for index,path in enumerate(paths):
        x=(index%4)*536+12;y=(index//4)*298+30
        sheet.paste(Image.open(path).resize((512,256),Image.Resampling.NEAREST),(x,y))
        draw.text((x,y-22),path.stem,fill=(20,20,20))
    sheet.save(args.output/"all-screens.png")
    evidence={
        "schema":1,"reference":args.ref or "working-tree",
        "renderer":"production C callbacks + repository U8g2 and PNG icons",
        "models":"controlled display fixtures; no radio or physical LCD timing",
        "generated_c_sha256":hashlib.sha256(generated_source.encode()).hexdigest(),
        "address_sanitizer":args.sanitize,
        "navigation":"1000 up and down steps, bounded left/right and change callbacks",
        "text_input":"all cursor positions for lengths 0..79" if not args.skip_text_input else "skipped: known baseline stack overflow",
        "frames":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    }
    (args.output/"evidence.json").write_text(json.dumps(evidence,indent=2)+"\n")
    if args.compare:
        selected=[("03-editor-head","Editor: ID and frequency"),("06-config","Radio configuration"),("16-details-long","Station: long name and ID"),("20-about","About")]
        comparison=Image.new("RGB",(1584,4*436+48),(232,232,232))
        draw=ImageDraw.Draw(comparison)
        font=ImageFont.truetype("/System/Library/Fonts/Menlo.ttc",22)
        draw.text((16,8),"BEFORE",font=font,fill="black");draw.text((808,8),"AFTER",font=font,fill="black")
        for i,(name,label) in enumerate(selected):
            y=48+i*436
            for x,directory in [(16,args.compare),(808,args.output)]:
                draw.text((x,y),label,font=font,fill="black")
                comparison.paste(Image.open(directory/(name+".pgm")).resize((768,384),Image.Resampling.NEAREST),(x,y+30))
        comparison.save(args.output/"comparison.png")
    print(args.output.resolve())


if __name__=="__main__":main()
