"""Host preview from production loading draw calls and the actual icon frames.

Run with the firmware toolchain Python. This is a layout preview, not a recording
from hardware; progress timing is illustrative.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw
from test_hotplug_assets import ROOT, function


def trace(progress):
    loading = (ROOT / "applications/services/gui/modules/loading.c").read_text()
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    defines = "\n".join(re.findall(r"^#define LOADING_.*$", loading, re.MULTILINE))
    code = r"""
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <assert.h>
#define furi_check assert
enum { ColorWhite=255, ColorBlack=0 };
typedef struct { int color; } Canvas;
typedef struct { void* icon; bool progress_shown;float progress; } LoadingModel;
static int A_Loading_24;
static int canvas_width(Canvas* c) { (void)c;return 128; }
static int canvas_height(Canvas* c) { (void)c;return 64; }
static void canvas_set_color(Canvas* c,int v) { c->color=v; }
static void canvas_draw_box(Canvas* c,int x,int y,size_t w,size_t h) {
 printf("box %d %d %zu %zu %d\n",x,y,w,h,c->color);
}
static void canvas_draw_rframe(Canvas* c,int x,int y,size_t w,size_t h,int r) {
 printf("frame %d %d %zu %zu %d %d\n",x,y,w,h,c->color,r);
}
static void canvas_draw_icon(Canvas* c,int x,int y,void* i) { (void)c;(void)x;(void)y;(void)i; }
static void canvas_draw_icon_animation(Canvas* c,int x,int y,void* i) {
 (void)c;(void)i;printf("icon %d %d\n",x,y);
}
"""
    code += defines + "\n"
    code += function(elements, "void elements_progress_bar(") + "\n"
    code += function(loading, "static void loading_draw_callback(") + "\n"
    code += (
        "int main(void) { Canvas c={0}; LoadingModel m={.progress_shown="
        + ("false" if progress is None else "true")
        + ",.progress=" + str(progress or 0) + "};loading_draw_callback(&c,&m);return 0;}\n"
    )
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "draw.c").write_text(code)
        subprocess.run(["cc", str(path / "draw.c"), "-lm", "-o", str(path / "draw")], check=True)
        return subprocess.check_output([str(path / "draw")], text=True).splitlines()


def render(commands, icon):
    image = Image.new("L", (128, 64), 255)
    draw = ImageDraw.Draw(image)
    for line in commands:
        kind, *args = line.split()
        values = list(map(int, args))
        if kind == "icon":
            x, y = values
            assert 0 <= x <= 104 and 0 <= y <= 40
            image.paste(icon.convert("L"), (x, y))
        else:
            x, y, w, h, color, *radius = values
            assert x >= 0 and y >= 0 and x+w <= 128 and y+h <= 64
            if not w or not h:
                continue
            rect = (x, y, x+w-1, y+h-1)
            if kind == "frame":
                draw.rounded_rectangle(rect, radius=radius[0], outline=color)
            else:
                draw.rectangle(rect, fill=color)
    return image.resize((768, 384), Image.Resampling.NEAREST)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    icons = [Image.open(p).copy() for p in sorted(
        (ROOT / "assets/icons/Common/Loading_24").glob("frame_*.png"))]
    frames = []
    for step in range(21):
        frames.append(render(trace(step / 20), icons[step % len(icons)]))
    frames[0].save(args.output / "assets-0.png")
    frames[10].save(args.output / "assets-50.png")
    frames[-1].save(args.output / "assets-100.png")
    render(trace(None), icons[0]).save(args.output / "loading-only.png")
    frames[0].save(args.output / "assets-progress.gif", save_all=True,
                   append_images=frames[1:], duration=160, loop=0)
    print(args.output / "assets-progress.gif")


if __name__ == "__main__":
    main()
