#!/usr/bin/env python3
"""Render a proposed TumoSpectrum archive flow with production GUI components.

The navigation uses the firmware's Submenu renderer. Detail/status screens use
the firmware Canvas, U8g2 fonts and button affordances in the existing
TumoSpectrum visual style; device input/storage are not simulated.
"""
import argparse
from pathlib import Path
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

try:
    from .render_inspector_native import build_source
except ImportError:
    from render_inspector_native import build_source

ROOT = Path(__file__).resolve().parents[2]


def build_preview_source():
    source = build_source()
    main_start = source.index("int main(int argc,char** argv) {")
    prefix = source[:main_start]
    components = r'''
static void archive_header(Canvas* c,const char* title,const char* badge) {
 canvas_clear(c);canvas_set_font(c,FontPrimary);canvas_draw_str(c,2,11,title);
 canvas_draw_rframe(c,80,0,48,13,2);canvas_set_font(c,FontSecondary);
 canvas_draw_str_aligned(c,104,10,AlignCenter,AlignBottom,badge);
 canvas_draw_line(c,0,15,127,15);
}
static void archive_buttons(Canvas* c,const char* left,const char* center) {
 elements_button_left(c,left);elements_button_center(c,center);
}
static void archive_set_summary(Canvas* c) {
 archive_header(c,"Capture Set","4/4 OK");canvas_set_font(c,FontPrimary);
 canvas_draw_str(c,3,27,"Garage");canvas_set_font(c,FontSecondary);
 canvas_draw_str_aligned(c,126,26,AlignRight,AlignBottom,"10/11");
 canvas_draw_str(c,3,40,"SUB RAW");
 canvas_draw_str(c,3,50,"433.920 MHz  AM650");
 archive_buttons(c,"Back","Open");
}
static void archive_set_actions(Canvas* c,const char* dir) {
 const char* items[]={"Open saved analysis","Check source files","Export new report","Back to archive"};
 menu(c,dir,"05-session-actions","Garage - 10/11",items,4,0,0);
}
static void archive_source_rows(Canvas* c,const char* badge,int failed,const char* problem) {
 archive_header(c,"Source Check",badge);canvas_set_font(c,FontSecondary);
 for(int i=0;i<4;i++){
  int y=24+i*8;char row[24];
  snprintf(row,sizeof(row),"%02d G%d.sub",i+1,i+1);
  canvas_draw_str(c,3,y,row);
  const char* status=i==failed?problem:"OK";
  canvas_draw_str_aligned(c,126,y,AlignRight,AlignBottom,status);
 }
 archive_buttons(c,"Back",failed>=0?"Review":"Done");
}
static void archive_legacy(Canvas* c) {
 archive_header(c,"Capture Set","LEGACY");canvas_set_font(c,FontPrimary);
 canvas_draw_str(c,3,34,"Saved result available");canvas_set_font(c,FontSecondary);
 canvas_draw_str(c,3,46,"Source files: unverified");
 archive_buttons(c,"Back","Open");
}
static void archive_empty(Canvas* c) {
 archive_header(c,"Saved Sets","EMPTY");canvas_set_font(c,FontPrimary);
 canvas_draw_str(c,3,34,"No saved sets");canvas_set_font(c,FontSecondary);
 canvas_draw_str(c,3,46,"Start a new set");
 archive_buttons(c,"Back","New");
}
static void archive_corrupt(Canvas* c) {
 archive_header(c,"Set Unavailable","ERROR");canvas_set_font(c,FontPrimary);
 canvas_draw_str(c,3,34,"Record cannot be read");canvas_set_font(c,FontSecondary);
 canvas_draw_str(c,3,46,"Other sets are untouched");
 archive_buttons(c,"Back","OK");
}
static void archive_exported(Canvas* c) {
 archive_header(c,"Report Export","SAVED");
 canvas_set_font(c,FontPrimary);canvas_draw_str(c,3,34,"New report created");
 canvas_set_font(c,FontSecondary);canvas_draw_str(c,3,46,"Source captures unchanged");
 archive_buttons(c,"Back","Open");
}
static void archive_storage_error(Canvas* c) {
 archive_header(c,"Source Check","SD ERROR");canvas_set_font(c,FontPrimary);
 canvas_draw_str(c,3,34,"SD read failed");canvas_set_font(c,FontSecondary);
 canvas_draw_str(c,3,46,"Saved result unchanged");
 archive_buttons(c,"Back","Retry");
}
'''
    main = r'''
int main(int argc,char** argv) {
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&c,FontSecondary);canvas_set_color(&c,ColorBlack);
 const char* main_menu[]={"Band Map","Capture Sub-GHz RAW","Capture Infrared RAW","Open Saved Sub-GHz","Open Saved Infrared","New Sub-GHz Set","New Infrared Set","Capture Sets","Open TumoScope","Import Analyzer","Protocol Profiles","Notebook","About"};
 menu(&c,argv[1],"01-main-menu","TumoSpectrum",main_menu,13,7,6);
 const char* sets_menu[]={"Latest set","Browse archive","About saved sets"};
 menu(&c,argv[1],"02-capture-sets","Capture Sets",sets_menu,3,1,0);
 const char* sessions[]={"Garage_Gate_20261011","GateB_Press_20261009","Weather_Sensor_20261008","IR_Remote_20261002","Meter_20260929"};
 menu(&c,argv[1],"03-archive-top","Saved sets",sessions,5,0,0);
 menu(&c,argv[1],"04-archive-scroll","Saved sets",sessions,5,4,2);
 archive_set_actions(&c,argv[1]);
 archive_set_summary(&c);save(&c,argv[1],"06-session-summary");
 archive_source_rows(&c,"4/4 OK",-1,"");save(&c,argv[1],"07-sources-intact");
 archive_source_rows(&c,"3/4 OK",2,"CHANGED");save(&c,argv[1],"08-source-changed");
 archive_source_rows(&c,"2/4 OK",2,"MISSING");save(&c,argv[1],"09-source-missing");
 archive_legacy(&c);save(&c,argv[1],"10-legacy-unverified");
 archive_empty(&c);save(&c,argv[1],"11-empty");
 archive_corrupt(&c);save(&c,argv[1],"12-corrupt-entry");
 archive_exported(&c);save(&c,argv[1],"13-export-success");
 archive_storage_error(&c);save(&c,argv[1],"14-storage-error");
 return 0;
}
'''
    return prefix + components + main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="signal-archive-preview-") as directory:
        temp = Path(directory)
        src = temp / "render.c"
        src.write_text(build_preview_source())
        sources = [path for path in (ROOT / "lib/u8g2").glob("*.c") if path.name != "u8g2_glue.c"]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        subprocess.run(
            [
                "cc", "-std=c11", "-O1", "-g", "-w", "-ffunction-sections", gc,
                "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"), str(src),
                *map(str, sources), "-lm", "-o", str(temp / "render"),
            ],
            check=True,
        )
        subprocess.run(
            [str(temp / "render"), str(args.output.resolve())],
            check=True,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
        )

    frames = sorted(args.output.glob("*.pgm"))
    assert len(frames) == 14, f"expected 14 screens, got {len(frames)}"
    sheet = Image.new("RGB", (4 * 404, 4 * 232), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        with Image.open(path) as image:
            assert image.size == (128, 64), f"unexpected screen size: {path} {image.size}"
            image.save(path.with_suffix(".png"))
            x, y = (index % 4) * 404 + 10, (index // 4) * 232 + 28
            sheet.paste(image.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
            draw.text((x, y - 20), path.stem, fill="black")
        path.unlink()
    output = args.output / "signal-session-archive-contact-sheet.png"
    sheet.save(output)
    print(output)


if __name__ == "__main__":
    main()
