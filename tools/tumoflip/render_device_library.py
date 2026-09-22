"""Native 128x64 previews of Device Library and Sensor Helper; not hardware acceptance."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw
from tools.tumoflip import render_inspector_native as inspector
from tools.tumoflip import render_weather_native as gui

ROOT = Path(__file__).resolve().parents[2]


def build_source():
    source = inspector.build_source().split("int main(int argc,char** argv)", 1)[0]
    app = (ROOT / "applications/system/device_library/device_library.c").read_text()
    workbench = (ROOT / "applications_user/signal_workbench/signal_workbench.c").read_text()
    source += app[app.index("enum {"):app.index("typedef struct {")]
    model = (ROOT / "applications/system/device_library/library_model.h").read_text()
    source += re.sub(r"^#(?:pragma|include).*\n", "\n", model, flags=re.M)
    source += re.sub(r"^#(?:pragma|include).*\n", "\n", (ROOT / "applications_user/signal_workbench/sensor_fit.h").read_text(), flags=re.M)
    source += r'''
typedef struct{uint32_t generation,size;}FileHistoryRecord;
typedef struct{bool(*list)(void*,const char*,FileHistoryRecord*);}HistoryApi;
typedef struct{SubmenuModel*menu;DeviceCard card;bool dirty;unsigned card_offset;void*storage;void*dispatcher;FuriString*path;const HistoryApi*history;FileHistoryRecord records[4];int page;}Library;
static const char*output_dir;static Canvas*screen;static int history_state;
static bool history_core=true;
static bool file_history_core_available(void){return history_core;}
enum{FSE_OK,FSE_NOT_EXIST};
typedef int FS_Error;
#define FILE_HISTORY_ENABLED "flag"
static int storage_common_stat(void*s,const char*p,void*i){(void)s;(void)p;(void)i;return history_state;}
static void submenu_reset(SubmenuModel*m){for(unsigned i=0;i<SubmenuItemArray_size(m->items);i++)furi_string_free(SubmenuItemArray_get(m->items,i)->label);SubmenuItemArray_reset(m->items);}
static void submenu_set_header(SubmenuModel*m,const char*h){canvas_set_font(screen,FontPrimary);if(canvas_string_width(screen,h)>124)fprintf(stderr,"Header too wide: %s (%u px)\n",h,canvas_string_width(screen,h));assert(canvas_string_width(screen,h)<=124);furi_string_set(m->header,h);}
static void library_action(void*c,uint32_t e){(void)c;(void)e;}
static void submenu_add_item(SubmenuModel*m,const char*l,uint32_t id,void*cb,void*ctx){(void)id;(void)cb;(void)ctx;SubmenuItem i={.label=furi_string_alloc_set(l)};SubmenuItemArray_push_back(m->items,i);}
static void library_item(Library*a,const char*l,unsigned id){submenu_add_item(a->menu,l,id,library_action,a);}
static void library_cards(Library*a){library_item(a,"Weather station",100);library_item(a,"Living room AC",101);library_item(a,"Long custom device name",102);}
static bool library_history_load(Library*a){(void)a;return true;}
static bool list_versions(void*s,const char*p,FileHistoryRecord*r){(void)s;(void)p;for(unsigned i=0;i<4;i++)r[i]=(FileHistoryRecord){i<3?3-i:0,1280};return true;}
static void view_dispatcher_switch_to_view(void*d,int id){(void)d;(void)id;}
static void render_menu(Canvas*c,const char*dir,const char*name,SubmenuModel*m,int position,int window){m->position=position;m->window_position=window;submenu_view_draw_callback(c,m);save(c,dir,name);}
enum{TumoSpectrumViewSensorMenu,SensorValue,SensorAnalyze=20,SensorExport,SensorAbout,SensorEdit,SensorCandidateBase=0x5100};
typedef struct{SubmenuModel*sensor_menu;int32_t sensor_values[4];uint8_t sensor_entered;void*view_dispatcher;SensorFitResult sensor_result;}TumoSpectrumApp;
static void tumospectrum_sensor_action(void*c,uint32_t e){(void)c;(void)e;}
'''
    source += gui.function((ROOT / "applications/system/device_library/library_model.c").read_text(), "int library_newest_slot(")
    source += gui.function(app, "static void library_show(")
    source += gui.function(workbench, "static void tumospectrum_sensor_menu(")
    source += gui.function(workbench, "static void tumospectrum_sensor_results_menu(")
    fit = (ROOT / "applications_user/signal_workbench/sensor_fit.c").read_text()
    source += gui.function(fit, "static void sensor_decimal(")
    source += gui.function(fit, "bool sensor_candidate_text(")
    source += r'''
int main(int argc,char**argv){assert(argc==2);Canvas c={0};screen=&c;output_dir=argv[1];static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);canvas_set_color(&c,ColorBlack);
 SubmenuModel menu_model={0};SubmenuItemArray_init(menu_model.items);menu_model.header=furi_string_alloc();
 HistoryApi api={list_versions};Library app={.menu=&menu_model,.history=&api,.path=furi_string_alloc_set("/ext/subghz/Weather.sub"),.dirty=true};
 strcpy(app.card.name,"Weather station");app.card.link_count=2;strcpy(app.card.links[0],"/ext/subghz/Weather.sub");strcpy(app.card.links[1],"/ext/subghz/profiles/Outdoor.subprofile");
 library_show(&app,PageMain);render_menu(&c,argv[1],"01-library",app.menu,0,0);
 library_show(&app,PageCards);render_menu(&c,argv[1],"02-cards",app.menu,0,0);
 library_show(&app,PageCard);render_menu(&c,argv[1],"03-card",app.menu,0,0);render_menu(&c,argv[1],"04-card-bottom",app.menu,5,5);
 strcpy(app.card.name,"WWWWWWWWWWWWWWWWWWWWWWWW");library_show(&app,PageCard);render_menu(&c,argv[1],"05-card-long",app.menu,0,0);
 library_show(&app,PageLink);render_menu(&c,argv[1],"06-link",app.menu,0,0);
 history_state=FSE_NOT_EXIST;library_show(&app,PageHistory);render_menu(&c,argv[1],"07-history-off",app.menu,0,0);
 history_state=FSE_OK;library_show(&app,PageHistory);render_menu(&c,argv[1],"08-history-on",app.menu,0,0);
 history_state=99;library_show(&app,PageHistory);render_menu(&c,argv[1],"09-history-error",app.menu,0,0);
 library_show(&app,PageVersions);render_menu(&c,argv[1],"10-versions",app.menu,1,0);
 library_show(&app,PageDiscard);render_menu(&c,argv[1],"11-unsaved",app.menu,0,0);
 text_page(&c,argv[1],"12-card-details","Weather station\n\nNotes:\nOutdoor sensor near window\n\nTags:\nhome outdoor\nLinks: 2\n\nManually checked:\n2026-09-22 14:35",0);
 text_page(&c,argv[1],"13-card-details-bottom","Weather station\n\nNotes:\nOutdoor sensor near window\n\nTags:\nhome outdoor\nLinks: 2\n\nManually checked:\n2026-09-22 14:35",8);
 text_page(&c,argv[1],"14-restored","Restored as new copy:\n/ext/subghz/Weather_restored_00.sub\n\nCurrent file unchanged.",0);
 text_page(&c,argv[1],"15-restore-error","Restore failed.\nBackup missing/corrupt,\nSD error or path too long.\nCurrent file unchanged.",0);
 text_page(&c,argv[1],"16-save-error","Save failed.\nPrevious revision retained.",0);
 text_page(&c,argv[1],"17-private","History enabled.\nNative SUB / IR / NFC.\n3 versions, 2 MiB max.\nBackups may be private.",0);
 history_core=false;history_state=FSE_OK;library_show(&app,PageHistory);render_menu(&c,argv[1],"18-old-firmware",app.menu,0,0);history_core=true;
 SubmenuModel sm={0};SubmenuItemArray_init(sm.items);sm.header=furi_string_alloc();
 TumoSpectrumApp spectrum={.sensor_menu=&sm,.sensor_values={101,153,227,319},.sensor_entered=15};
 tumospectrum_sensor_menu(&spectrum);render_menu(&c,argv[1],"20-sensor-values",&sm,0,0);render_menu(&c,argv[1],"21-sensor-check",&sm,4,3);
 NumberInputModel input={.header=furi_string_alloc_set("Sample 1: value x10"),.text_buffer=furi_string_alloc_set("245"),.current_number=245,.min_value=-1000000,.max_value=1000000};
 canvas_clear(&c);canvas_set_color(&c,ColorBlack);number_input_view_draw_callback(&c,&input);save(&c,argv[1],"22-measurement");
 text_page(&c,argv[1],"23-hypotheses","Sensor hypotheses v1\n8 fits; showing 8\n\nNot a proven decoder.\nFirst 3 samples train;\n4th is held out.\nValues below are x10.",0);
 char result[192];SensorCandidate candidate={.start=8,.width=16,.scale10=1,.predicted10=319,.holdout_match=true};
 assert(sensor_candidate_text(&candidate,1,319,result,sizeof(result)));text_page(&c,argv[1],"24-match",result,0);
 spectrum.sensor_result.count=2;spectrum.sensor_result.total=8;spectrum.sensor_result.candidates[0]=candidate;
 candidate.start=16;candidate.width=8;candidate.predicted10=63;candidate.holdout_match=false;
 assert(sensor_candidate_text(&candidate,2,319,result,sizeof(result)));text_page(&c,argv[1],"25-failed-check",result,0);
 spectrum.sensor_result.candidates[1]=candidate;tumospectrum_sensor_results_menu(&spectrum);render_menu(&c,argv[1],"28-hypothesis-list",&sm,0,0);
 text_page(&c,argv[1],"26-no-fit","No matching hypothesis.\nNeed distinct training\nvalues or another encoding.",0);
 text_page(&c,argv[1],"27-incomplete","Need four short, complete\nRAW files from one sensor,\nsame frequency / preset,\nand four measured values.\nNo hypothesis generated.",0);
 return 0;}
'''
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("output",type=Path);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="library-ui-") as directory:
        tmp=Path(directory);(tmp/"render.c").write_text(build_source())
        sources=[p for p in (ROOT/"lib/u8g2").glob("*.c") if p.name!="u8g2_glue.c"]
        subprocess.run(["cc","-std=c11","-O1","-g","-w","-fsanitize=address,undefined","-ffunction-sections","-Wl,-dead_strip",
            "-I",str(ROOT/"lib/u8g2"),"-I",str(ROOT/"lib/mlib"),str(tmp/"render.c"),*map(str,sources),"-lm","-o",str(tmp/"render")],check=True)
        subprocess.run([str(tmp/"render"),str(args.output.resolve())],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
    frames=sorted(args.output.glob("*.pgm"));sheet=Image.new("RGB",(3*404,((len(frames)+2)//3)*224),"#ececec");draw=ImageDraw.Draw(sheet)
    for i,path in enumerate(frames):
        frame=Image.open(path);frame.resize((768,384),Image.Resampling.NEAREST).save(path.with_suffix(".png"));x,y=i%3*404+10,i//3*224+24
        sheet.paste(frame.resize((384,192),Image.Resampling.NEAREST).convert("RGB"),(x,y));draw.text((x,y-19),path.stem,fill="black")
    sheet.save(args.output/"all-screens.png")
    selected=["03-card","06-link","08-history-on","10-versions","28-hypothesis-list","24-match"]
    overview=Image.new("RGB",(808,3*224),"#ececec");draw=ImageDraw.Draw(overview)
    for i,name in enumerate(selected):
        frame=Image.open(args.output/(name+".pgm"));x,y=i%2*404+10,i//2*224+24
        overview.paste(frame.resize((384,192),Image.Resampling.NEAREST).convert("RGB"),(x,y));draw.text((x,y-19),name,fill="black")
    overview.save(args.output/"overview.png")
    (args.output/"report.json").write_text(json.dumps({"frames":len(frames),"hardware_test":False,"renderer":"production menu constructors / GUI draw callbacks with controlled fixtures"},indent=2))
    print(args.output/"overview.png")


if __name__=="__main__":main()
