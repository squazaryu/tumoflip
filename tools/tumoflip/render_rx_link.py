"""Render actual RX Link draw code with firmware fonts; not hardware acceptance."""
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
    source += r'''
static void rx_preview_str(Canvas* c,int32_t x,int32_t y,const char* text) {
 assert(y<=49);
 if(x<0 || x+canvas_string_width(c,text)>128) fprintf(stderr,"Clipped text at %d,%d: %s (%d px)\n",x,y,text,(int)canvas_string_width(c,text));
 assert(x>=0 && x+canvas_string_width(c,text)<=128);
 canvas_draw_str(c,x,y,text);
}
static void rx_preview_aligned(Canvas* c,int32_t x,int32_t y,Align horizontal,Align vertical,const char* text) {
 assert(y<=49 || (x==126 && y==63 && horizontal==AlignRight));
 int width=canvas_string_width(c,text);
 int left=horizontal==AlignRight?x-width:horizontal==AlignCenter?x-width/2:x;
 assert(left>=0 && left+width<=128);
 canvas_draw_str_aligned(c,x,y,horizontal,vertical,text);
}
#define canvas_draw_str rx_preview_str
#define canvas_draw_str_aligned rx_preview_aligned
'''
    for name in ("rx_link.h", "rx_link_view.h", "rx_link_view.c"):
        text = (ROOT / "applications_user/tumo_rx_link" / name).read_text()
        source += re.sub(r"^#(?:include|pragma).*$", "", text, flags=re.M) + "\n"
    source += native.function(native.DRIVER, "static void save(") + r'''
int main(int argc,char** argv) {
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 RxLinkViewModel m={.frequency=1};
 rx_link_draw(&c,&m);save(&c,argv[1],"01-idle");
 m.frequency=2;m.modulation=1;rx_link_draw(&c,&m);save(&c,argv[1],"01b-idle-fm");m.frequency=1;m.modulation=0;
 m.phase=RxUiWaiting;rx_link_draw(&c,&m);save(&c,argv[1],"02-waiting");
 m.phase=RxUiLive;rx_link_draw(&c,&m);save(&c,argv[1],"02b-empty");
 m.snapshot=(RxLinkSnapshot){.count=10000,.minimum=120,.mean=480,.maximum=960,.high_permille=500};
 m.snapshot.bins[6]=500;m.snapshot.bins[7]=3000;m.snapshot.bins[8]=2000;m.snapshot.bins[9]=4000;m.snapshot.bins[10]=500;
 rx_link_draw(&c,&m);save(&c,argv[1],"03-histogram");
 m.page=1;rx_link_draw(&c,&m);save(&c,argv[1],"04-timings");
 m.page=2;m.local_drops=UINT32_MAX;m.snapshot.lost_batches=UINT32_MAX;m.snapshot.rejected_batches=UINT32_MAX;m.uart_errors=UINT32_MAX;
 rx_link_draw(&c,&m);save(&c,argv[1],"05-loss-limits");
 m.page=3;rx_link_draw(&c,&m);save(&c,argv[1],"05b-link-errors");
 m.phase=RxUiStopped;rx_link_draw(&c,&m);save(&c,argv[1],"06-stopped");
 for(unsigned error=RxErrorUart;error<=RxErrorRadioLost;error++) {
  char name[32];snprintf(name,sizeof(name),"%02u-error-%u",error+6,error);
  m.phase=RxUiError;m.error=error;rx_link_draw(&c,&m);save(&c,argv[1],name);
 }
 return 0;
}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rx-link-render-") as directory:
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
    assert len(frames) == 15
    sheet = Image.new("RGB", (3 * 404, 5 * 232), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(frames):
        with Image.open(path) as image:
            assert image.size == (128, 64)
            image.save(path.with_suffix(".png"))
            x, y = (i % 3) * 404 + 10, (i // 3) * 232 + 28
            sheet.paste(image.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
            draw.text((x, y - 20), path.stem, fill="black")
    sheet.save(args.output / "contact-sheet.png")
    print(args.output / "contact-sheet.png")


if __name__ == "__main__":
    main()
