"""Render actual Envelope Check callbacks / stock GUI, never a mock-up or device."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw
from tools.tumoflip import render_field_radio as radio
from tools.tumoflip.render_weather_native import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/signal_workbench"


def build_source():
    source = radio.build_source().split("int main(int argc,char**argv)", 1)[0]
    for name in ("tumospectrum_envelope_stats.h", "tumospectrum_envelope.h",
                 "tumospectrum_envelope_stats.c"):
        source += re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / name).read_text(), flags=re.M)
    source += r'''
typedef struct {TumoEnvelopeSnapshot snapshot;} TumoSpectrumEnvelopeModel;
static struct {int left,right,y;} text_bounds[16];static unsigned text_count;
static int graph_min_y,graph_max_y;
static void envelope_bounds(Canvas*c,int left,int y,const char*t){
 int right=left+canvas_string_width(c,t);assert(left>=0&&right<=128&&y>=7&&y<=64);
 if(y==50){assert(y-u8g2_GetAscent(&c->fb)>40);assert(y-u8g2_GetDescent(&c->fb)<54);}
 for(unsigned i=0;i<text_count;i++)if(text_bounds[i].y==y){
  if(left<text_bounds[i].right+2&&right+2>text_bounds[i].left){fprintf(stderr,"Overlapping envelope text: %s at y%d\n",t,y);abort();}}
 assert(text_count<16);text_bounds[text_count].left=left;text_bounds[text_count].right=right;text_bounds[text_count++].y=y;
}
static void envelope_str(Canvas*c,int x,int y,const char*t){envelope_bounds(c,x,y,t);canvas_draw_str(c,x,y,t);}
static void envelope_aligned(Canvas*c,int x,int y,Align h,Align v,const char*t){int w=canvas_string_width(c,t);envelope_bounds(c,x-(h==AlignCenter?w/2:h==AlignRight?w:0),y,t);canvas_draw_str_aligned(c,x,y,h,v,t);}
static void envelope_line(Canvas*c,int x1,int y1,int x2,int y2){
 assert(x1>=0&&x2<=127&&y1>=0&&y2<=63);
 if(x1==x2){assert(y1<=40&&y2<=40&&y1>=24&&y2>=24);}
 if(x1==x2){if(y2<graph_min_y)graph_min_y=y2;if(y2>graph_max_y)graph_max_y=y2;}
 canvas_draw_line(c,x1,y1,x2,y2);
}
#define canvas_draw_str envelope_str
#define canvas_draw_str_aligned envelope_aligned
#define canvas_draw_line envelope_line
'''
    workbench = (APP / "signal_workbench.c").read_text()
    source += function(workbench, "static const char* tumospectrum_envelope_status_name(")
    source += function(workbench, "static void tumospectrum_envelope_draw(")
    source += "\n#undef canvas_draw_str\n#undef canvas_draw_str_aligned\n#undef canvas_draw_line\n"
    source += r'''
int main(int argc,char**argv){
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);canvas_set_color(&c,ColorBlack);
 TumoSpectrumEnvelopeModel m={.snapshot={.status=TumoEnvelopeRunning,.frequency_hz=433920000,.floor_dbm=-105,.high_dbm=-60}};
 const char*names[]={"01-calibrate","02-learning","03-no-signal","04-pulsed","05-flat-carrier","06-unclear","07-overload","08-stopped","09-radio-busy","10-no-radio","11-read-error","12-long-values","13-weak-pulsed"};
 for(unsigned state=0;state<13;state++){
  TumoEnvelopeSnapshot*s=&m.snapshot;s->status=TumoEnvelopeRunning;s->calibrated=state>0;s->external=state%2;s->frequency_hz=state==11?915000000:433920000;s->agreement=0;s->floor_dbm=-105;s->high_dbm=state==2?-105:state==6?-20:-60;
  s->kind=state==1?TumoEnvelopeLearning:state==2?TumoEnvelopeNoSignal:state==3?TumoEnvelopePulsed:state==4?TumoEnvelopeFlat:state==5?TumoEnvelopeUnclear:state==6?TumoEnvelopeTooStrong:TumoEnvelopeInvalid;
  if(state>=8&&state<=10)s->status=state==8?TumoEnvelopeBusy:state==9?TumoEnvelopeMissing:TumoEnvelopeError;
  if(state==7)s->status=TumoEnvelopeIdle;
  if(state>=7&&state<=10){s->calibrated=false;s->floor_dbm=-127;s->high_dbm=-127;}
  if(state==3||state==4)s->agreement=100;
  if(state==11){s->kind=TumoEnvelopeFlat;s->agreement=100;s->floor_dbm=-138;s->high_dbm=-31;}
  if(state==12){s->kind=TumoEnvelopePulsed;s->agreement=100;s->floor_dbm=-138;s->high_dbm=-120;}
  for(unsigned i=0;i<128;i++)s->samples[i]=state==3?(i%16<8?-60:-105):state==5?(i%16<8?-60:-83):state==2?-105:state==6?-20:state==11?-31:-60;
  if(state==12)for(unsigned i=0;i<128;i++)s->samples[i]=i%16<8?-120:-138;
  text_count=0;graph_min_y=100;graph_max_y=-1;tumospectrum_envelope_draw(&c,&m);
  if(state==12)assert(graph_min_y<graph_max_y); // weak pulse must not disappear into clipping
  save(&c,argv[1],names[state]);
 }
 const char*items[]={"Band Map","Envelope Check","Capture Sub-GHz RAW","Capture Infrared RAW"};
 menu(&c,argv[1],"14-menu","TumoSpectrum",items,4,1,0);
'''
    source += 'text_page(&c,argv[1],"15-about",' + radio.message(workbench, "Autonomous receive-only Band Map,") + ',0);\n'
    source += 'text_page(&c,argv[1],"16-guide",' + radio.message(workbench, "Envelope Check:") + ',0);\nreturn 0;}'
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    output = parser.parse_args().output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="envelope-ui-") as directory:
        work = Path(directory)
        (work / "render.c").write_text(build_source())
        sources = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
            "-ffunction-sections", "-Wl,-dead_strip", "-I", str(ROOT / "lib/u8g2"),
            "-I", str(ROOT / "lib/mlib"), str(work / "render.c"), *map(str, sources),
            "-lm", "-o", str(work / "render")], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        subprocess.run([str(work / "render"), str(output)], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    frames = sorted(output.glob("*.pgm"))
    if len(frames) != 16:
        raise RuntimeError(f"Expected 16 frames, got {len(frames)}")
    sheet = Image.new("RGB", (3 * 404, 6 * 224), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        frame = Image.open(path).convert("RGB")
        frame.save(path.with_suffix(".png"))
        x, y = index % 3 * 404 + 10, index // 3 * 224 + 24
        draw.text((x, y - 19), path.stem, fill="black")
        sheet.paste(frame.resize((384, 192), Image.Resampling.NEAREST), (x, y))
    sheet.save(output / "contact-sheet.png")
    (output / "report.json").write_text(json.dumps({
        "frames": len(frames), "hardware_test": False,
        "renderer": "production Envelope Check callback, stock GUI and U8g2 fonts",
        "bounds_and_same_row_overlap_assertions": True,
    }, indent=2) + "\n")
    print(f"Rendered {len(frames)} production-view frames")


if __name__ == "__main__":
    main()
