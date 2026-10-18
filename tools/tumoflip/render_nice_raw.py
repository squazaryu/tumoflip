"""Render actual Nice draw code with firmware fonts, not a hardware screenshot."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as native

ROOT = Path(__file__).resolve().parents[2]


def build_source():
    source = native.build_source(None).split("static int changes;", 1)[0]
    nice = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    source += "\n#include <math.h>\n"
    for signature in ("void elements_frame(", "void elements_progress_bar(",
                      "void elements_progress_bar_with_text("):
        source += native.function(elements, signature) + "\n"
    source += r'''
#define furi_mutex_acquire(...) ((void)0)
#define furi_mutex_release(...) ((void)0)
#define NICE_O_MIN_KEYS 4
#define NICE_O_SWEEP_TOTAL 65536
typedef enum {NiceOSceneMenu,NiceOSceneCapture,NiceOSceneRaw,NiceOSceneSolve,NiceOSceneResult} NiceOScene;
typedef struct {
 void* mutex;NiceOScene scene;uint8_t radio_indicator,menu_index,key_count,raw_progress;
 unsigned long packets,sweep_ic,serial,certificate;unsigned ic;
 bool solved,certificate_known,certificate_conflict,offline;FuriString* status;
} NiceOCode;
static void preview_str(Canvas* c,int32_t x,int32_t y,const char* text){
 int w=canvas_string_width(c,text);
 if(x<0||x+w>128||y>63)fprintf(stderr,"Clipped: %s at %d,%d width %d\n",text,x,y,w);
 assert(x>=0&&x+w<=128&&y<=63);canvas_draw_str(c,x,y,text);
}
#define canvas_draw_str preview_str
static void preview_frame(Canvas* c,int32_t x,int32_t y,size_t w,size_t h){
 assert(x>=0&&y>=0&&x+w<=127&&y+h<=63);
 if(y==17||y==40)assert(y+h<52); /* footer starts at row52 */
 elements_frame(c,x,y,w,h);
}
#define elements_frame preview_frame
'''
    source += native.function(nice, "static void nice_o_draw_callback(")
    source += native.function(native.DRIVER, "static void save(")
    source += r'''
int main(int argc,char** argv){
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 NiceOCode a={.status=furi_string_alloc_set("Idle")};
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"01-menu");
 a.scene=NiceOSceneCapture;furi_string_set(a.status,"Listening 433.92");
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"02-live");
 a.key_count=16;a.packets=0xffffffffUL;furi_string_set(a.status,"Same press, ignored");
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"03-max-counts");
 a.scene=NiceOSceneRaw;a.offline=true;a.raw_progress=0;a.key_count=0;a.packets=0;
 furi_string_set(a.status,"Reading RAW file");nice_o_draw_callback(&c,&a);save(&c,argv[1],"04-raw-start");
 a.raw_progress=99;a.key_count=16;a.packets=0xffffffffUL;
 furi_string_set(a.status,"Same press, ignored");nice_o_draw_callback(&c,&a);save(&c,argv[1],"05-raw-reading");
 a.scene=NiceOSceneSolve;a.sweep_ic=65535;
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"06-solving");
 a.scene=NiceOSceneResult;a.offline=false;a.solved=true;a.ic=0xabcd;a.serial=0xfffffff;
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"07-result");
 a.offline=true;a.certificate_known=true;a.certificate=0;
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"08-zero-certificate");
 a.certificate=0xabcdef;
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"09-certificate");
 a.certificate_known=false;a.certificate_conflict=true;
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"10-conflict");
 a.solved=false;a.certificate_conflict=false;furi_string_set(a.status,"Invalid RAW file");
 nice_o_draw_callback(&c,&a);save(&c,argv[1],"11-invalid-file");
 furi_string_set(a.status,"No RAW samples");nice_o_draw_callback(&c,&a);save(&c,argv[1],"12-no-samples");
 furi_string_free(a.status);return 0;
}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nice-raw-render-") as directory:
        root = Path(directory)
        (root / "render.c").write_text(build_source())
        sources = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
                        "-ffunction-sections", gc, "-I", str(ROOT / "lib/u8g2"),
                        "-I", str(ROOT / "lib/mlib"), str(root / "render.c"),
                        *map(str, sources), "-lm", "-o", str(root / "render")], check=True)
        subprocess.run([str(root / "render"), str(args.output.resolve())], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    frames = sorted(args.output.glob("*.pgm"))
    assert len(frames) == 12
    sheet = Image.new("RGB", (1212, 928), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(frames):
        with Image.open(path) as frame:
            frame.save(path.with_suffix(".png"))
            x, y = (i % 3) * 404 + 10, (i // 3) * 232 + 28
            sheet.paste(frame.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
            draw.text((x, y - 20), path.stem, fill="black")
    sheet.save(args.output / "contact-sheet.png")
    print(args.output / "contact-sheet.png")


if __name__ == "__main__":
    main()
