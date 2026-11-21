"""Host regressions for safe text entry, rename and receiver presets.

Exercise actual firmware functions with a software canvas and in-memory paths.
No SD file, radio, device, or crash-reproduction payload is used.
"""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native as run_c


ROOT = Path(__file__).resolve().parents[2]
TEXT_INPUT = ROOT / "applications/services/gui/modules/text_input.c"

TEXT_SUPPORT = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define furi_check assert
#define with_view_model(view,decl,body,update) do {decl=(view);body;} while(0)
typedef int FuriString;
typedef void (*TextInputCallback)(void*);
typedef bool (*TextInputValidatorCallback)(const char*,FuriString*,void*);
typedef struct {void* view;} TextInput;
enum {ColorBlack,ColorWhite};
typedef struct {
 size_t width, drawn, width_used, box_width;
 bool caret, left, right;
 char text[128];
} Canvas;
uint16_t canvas_string_width(Canvas* c,const char* s){
 (void)c;size_t width=0;
 for(;*s;s++)width+=*s=='.'?2:*s=='|'?3:*s=='W'?7:5;
 return width;
}
size_t canvas_width(Canvas* c){return c->width;}
void canvas_set_color(Canvas* c,int color){(void)c;(void)color;}
void elements_slightly_rounded_box(Canvas* c,int x,int y,size_t width,size_t height){
 assert(x==3&&y==14&&height==10);assert(width<=c->width-6);c->box_width=width;
}
void canvas_draw_str(Canvas* c,int x,int y,const char* s){
 size_t width=canvas_string_width(c,s);assert(x>=4&&y==22);
 assert((size_t)x+width<=c->width-4);assert(x>=4+(int)c->width_used);
 c->width_used=(size_t)x+width-4;
 if(!strcmp(s,"...")){
  if(!c->drawn)c->left=true;else c->right=true;
 }else{
  assert(strlen(s)<sizeof(c->text));strcpy(c->text,s);c->caret=strchr(s,'|')!=NULL;
 }
 c->drawn++;
}
'''


def text_source():
    source = TEXT_INPUT.read_text()
    model = re.search(r"typedef struct \{\s+const char\* header;.*?\} TextInputModel;",
                      source, re.S).group()
    defines = "\n".join(re.findall(r"(?m)^#define TEXT_INPUT_DRAW_[^\n]+", source))
    helpers = ""
    for signature in ("static size_t text_input_length(", "static void text_input_draw_line("):
        if signature in source:
            helpers += function(source, signature) + "\n"
    return (TEXT_SUPPORT + model + "\n" + defines + "\n" + helpers
            + function(source, "void text_input_set_result_callback("))


RENAME_SUPPORT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert assert
#define SUBGHZ_APP_FOLDER "/ext/subghz"
#define SUBGHZ_APP_FILENAME_EXTENSION ".sub"
typedef struct {char text[384];} FuriString;
typedef int TextInput,DateTime,ValidatorIsFile;
typedef struct {const char* name;} Protocol;
typedef struct {Protocol* protocol;} SubGhzProtocolDecoderBase;
enum {SubGhzSceneSaveName=1,SubGhzSceneReadRAW,SubGhzCustomEventManagerNoSet=0,
      SubGhzCustomEventManagerSetRAW=3,SubGhzViewIdTextInput=1};
typedef struct {
 TextInput* text_input;FuriString *file_path,*file_path_tmp;bool save_datetime_set;
 DateTime save_datetime;void *txrx,*scene_manager,*view_dispatcher;
 struct {bool protocol_file_names;}* last_settings;
 char file_name_tmp[SUBGHZ_MAX_LEN_NAME];unsigned guard;
} SubGhz;
static unsigned entered,copied;
static char original[384];
FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
void furi_string_free(FuriString* s){free(s);}
const char* furi_string_get_cstr(const FuriString* s){return s->text;}
void furi_string_reset(FuriString* s){s->text[0]=0;}
void set_text(FuriString* s,const char* t){snprintf(s->text,sizeof(s->text),"%s",t);}
void set_string(FuriString* s,const FuriString* t){set_text(s,t->text);}
#define furi_string_set(s,t) _Generic((t),FuriString*:set_string,default:set_text)(s,t)
bool subghz_path_is_file(FuriString* s){return strstr(s->text,".sub")!=NULL;}
SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(void* txrx){(void)txrx;return NULL;}
unsigned scene_manager_get_scene_state(void* scene,unsigned id){(void)scene;(void)id;return 0;}
void name_generator_make_auto_datetime(char* s,size_t n,const char* prefix,DateTime* time){
 (void)prefix;(void)time;snprintf(s,n,"New signal");
}
void path_extract_dirname(const char* s,FuriString* d){(void)s;set_text(d,SUBGHZ_APP_FOLDER);}
void path_extract_filename(FuriString* s,FuriString* n,bool no_ext){
 const char* begin=strrchr(s->text,'/');begin=begin?begin+1:s->text;set_text(n,begin);
 if(no_ext){char* dot=strrchr(n->text,'.');assert(dot);*dot=0;}
}
/* Portable strlcpy fixture for both Linux CI and Mac. */
size_t strlcpy(char* d,const char* s,size_t n){
 size_t len=strlen(s);if(n){size_t copy=len<n?len:n-1;memcpy(d,s,copy);d[copy]=0;}return len;
}
void subghz_scene_save_name_text_input_callback(void* context){(void)context;assert(false);}
void text_input_set_header_text(TextInput* input,const char* s){(void)input;assert(!strcmp(s,"Name signal"));}
void text_input_set_result_callback(TextInput* input,void (*cb)(void*),void* context,char* name,size_t cap,bool clear){
 (void)input;(void)cb;SubGhz* s=context;
 assert(cap==sizeof(s->file_name_tmp)&&name==s->file_name_tmp);
 /* Only inspect the bounded copy: don't pass an unverified buffer to a reader. */
 assert(memchr(name,0,cap)!=NULL);
 assert(!strcmp(s->file_path->text,SUBGHZ_APP_FOLDER));
 if(original[0]){
  assert(!clear&&!strcmp(s->file_path_tmp->text,original));
  size_t len=strlen(strrchr(original,'/')+1)-4;
  if(len>=cap)len=cap-1;
  assert(strlen(name)==len&&!memcmp(name,strrchr(original,'/')+1,len));
 }else{
  assert(clear&&!strcmp(name,"New signal"));
 }
 assert(s->guard==0x1471);copied++;
}
ValidatorIsFile* validator_is_file_alloc_init(const char* p,const char* e,const char* old){
 static ValidatorIsFile validator;
 assert(!strcmp(p,SUBGHZ_APP_FOLDER)&&!strcmp(e,".sub")&&!*old);return &validator;
}
bool validator_is_file_callback(const char* name,FuriString* error,void* context){
 (void)name;(void)error;(void)context;return false;
}
void text_input_set_validator(TextInput* t,bool (*cb)(const char*,FuriString*,void*),void* v){
 (void)t;assert(cb&&v);
}
void view_dispatcher_switch_to_view(void* d,unsigned id){(void)d;assert(id==SubGhzViewIdTextInput);entered++;}
'''


class UpstreamNovember21Tests(unittest.TestCase):
    def test_bounded_draw_keeps_caret_and_selected_box_in_frame(self):
        # The new private renderer is an intentional missing-definition RED.
        # The old unbounded display routine is never invoked by this fixture.
        run_c(text_source() + r'''
void text_input_draw_line(Canvas*,const TextInputModel*,size_t);
int main(void){
 char text[430];TextInputModel model={.text_buffer=text,.text_buffer_size=sizeof(text)};
 TextInput input={.view=&model};
 const size_t widths[]={8,10,17,39,128};
 const size_t lengths[]={0,1,12,26,60,119,121,240,400};
 for(size_t l=0;l<sizeof(lengths)/sizeof(lengths[0]);l++){
  const size_t len=lengths[l];
  for(size_t i=0;i<len;i++)text[i]=i%3?'A':'W';
  text[len]='\0';
  for(size_t w=0;w<sizeof(widths)/sizeof(widths[0]);w++)
   for(size_t pos=0;pos<3;pos++)for(int selected=0;selected<=1;selected++){
    text_input_set_result_callback(&input,NULL,NULL,text,sizeof(text),selected);
    assert(model.cursor_pos==len);
    model.cursor_pos=pos==0?0:pos==1?len/2:len;
    Canvas canvas={.width=widths[w]};
    text_input_draw_line(&canvas,&model,len);
    if(canvas.width==128){
     assert(canvas.drawn&&canvas.caret==!selected);
     if(selected)assert(canvas.box_width==canvas.width_used+2);
     if(len==400&&selected)assert(canvas.right&&!canvas.left);
     if(len==400&&!selected&&pos==2)assert(canvas.left&&!canvas.right);
     if(len==400&&!selected&&pos==1)assert(canvas.left&&canvas.right);
    }
    for(size_t i=0;i<len;i++)assert(text[i]==(i%3?'A':'W'));
    assert(!text[len]);
   }
 }
 text_input_set_result_callback(&input,NULL,NULL,NULL,0,false);
 Canvas canvas={.width=128};text_input_draw_line(&canvas,&model,0);
 assert(canvas.caret&&!strcmp(canvas.text,"|"));
 return 0;
}
''')

    def test_binding_empty_and_zero_capacity(self):
        run_c(text_source() + r'''
int main(void){
 TextInputModel model={0};TextInput input={.view=&model};
 char label[8]="Entry";
 text_input_set_result_callback(&input,NULL,NULL,label,sizeof(label),false);
 assert(model.text_buffer==label && model.cursor_pos==5);
 assert(model.selected_row==2&&model.selected_column==8);
 text_input_set_result_callback(&input,NULL,NULL,label,0,false);
 assert(!model.text_buffer&&!model.text_buffer_size&&!model.cursor_pos);
 assert(!strcmp(label,"Entry"));
 return 0;
}
''')

    def test_standard_rename_passes_terminated_name_and_keeps_original_path(self):
        self.check_rename("applications/main/subghz")

    def test_arf_rename_passes_terminated_name_and_keeps_original_path(self):
        self.check_rename("applications_user/arf_subghz_full")

    def check_rename(self, directory):
        source = (ROOT / directory / "scenes/subghz_scene_save_name.c").read_text()
        header = (ROOT / directory / "subghz_i.h").read_text()
        limit = re.search(r"(?m)^#define SUBGHZ_MAX_LEN_NAME\s+(\d+)$", header).group(1)
        run_c("#define SUBGHZ_MAX_LEN_NAME " + limit + "\n" + RENAME_SUPPORT
              + function(source, "void subghz_scene_save_name_on_enter(") + r'''
int main(void){
 FuriString path={0},backup={0};TextInput input=0;
 SubGhz s={.text_input=&input,.file_path=&path,.file_path_tmp=&backup,.guard=0x1471};
 const size_t lengths[]={5,SUBGHZ_MAX_LEN_NAME-1,SUBGHZ_MAX_LEN_NAME,SUBGHZ_MAX_LEN_NAME+11};
 for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++){
  char filename[192];memset(filename,'A',lengths[i]);filename[lengths[i]]=0;
  snprintf(original,sizeof(original),"/ext/subghz/%s.sub",filename);
  set_text(&path,original);memset(s.file_name_tmp,'!',sizeof(s.file_name_tmp));
  subghz_scene_save_name_on_enter(&s);
 }
 original[0]=0;set_text(&path,SUBGHZ_APP_FOLDER);subghz_scene_save_name_on_enter(&s);
 assert(entered==5&&copied==5);return 0;
}
''')

    def test_both_frequencies_are_selectable_without_changing_hopper_or_default(self):
        source = (ROOT / "lib/subghz/subghz_setting.c").read_text()
        arrays = "\n".join(re.findall(
            r"static const uint32_t subghz_(?:hopper_)?frequency_list\[\] = \{.*?\};",
            source, re.S))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define FREQUENCY_FLAG_DEFAULT (UINT32_C(1)<<31)
''' + arrays + r'''
int main(void){
 unsigned low=0,high=0,defaults=0;uint32_t previous=0;
 for(size_t i=0;subghz_frequency_list[i];i++){
  uint32_t raw=subghz_frequency_list[i],frequency=raw&~FREQUENCY_FLAG_DEFAULT;
  if(frequency==317000000u)low++;
  if(frequency==336100000u)high++;
  if(raw&FREQUENCY_FLAG_DEFAULT){defaults++;assert(frequency==433920000u);}
  assert(frequency>previous);previous=frequency;
 }
 assert(low==1&&high==1&&defaults==1);
 const uint32_t hopper[]={315000000,390000000,430500000,433920000,434420000,868350000,0};
 assert(sizeof(hopper)==sizeof(subghz_hopper_frequency_list));
 assert(!memcmp(hopper,subghz_hopper_frequency_list,sizeof(hopper)));return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
