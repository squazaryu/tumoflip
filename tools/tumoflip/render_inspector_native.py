"""Native GUI component previews; no device or radio interaction.

Uses production Submenu/TextBox draw functions and firmware U8g2 fonts.
Fixtures are representative states, not a physical-device acceptance test.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

try:
    from . import render_weather_native as native
except ImportError:
    import render_weather_native as native

ROOT = Path(__file__).resolve().parents[2]


def build_source():
    source = native.build_source(None).split("static int changes;", 1)[0]
    textbox = (ROOT / "applications/services/gui/modules/text_box.c").read_text()
    additions = [
        "typedef enum {TextBoxFontText,TextBoxFontHex} TextBoxFont;",
        "typedef enum {TextBoxFocusStart,TextBoxFocusEnd} TextBoxFocus;",
        "\n".join(re.findall(r"^#define TEXT_BOX_.*$", textbox, re.M)),
        textbox[textbox.index("typedef struct {"):textbox.index("static void text_box_process_down(")],
        "static void host_cat_string(FuriString* d,const FuriString* s){furi_string_cat(d,s->data);}",
        "#define furi_string_cat(d,s) _Generic((s),FuriString*:host_cat_string,const FuriString*:host_cat_string,default:furi_string_cat)(d,s)",
    ]
    for signature in (
        "static bool text_box_end_of_text_reached(", "static bool text_box_start_of_text_reached(",
        "static void text_box_seek_next_line(", "static void text_box_seek_end_of_prev_line(",
        "static void text_box_seek_prev_paragraph(", "static void text_box_seek_prev_line(",
        "static void text_box_move_line_offset(", "static void text_box_update_screen_text(",
        "static void text_box_update_text_on_screen(", "static void text_box_prepare_model(",
        "static void text_box_view_draw_callback(",
    ):
        additions.append(native.function(textbox, signature))
    additions.append(native.function(native.DRIVER, "static void save("))
    additions.append(native.function(native.DRIVER, "static void menu("))
    additions.append(r'''
static void text_page(Canvas* c,const char* dir,const char* name,const char* text,int page) {
 TextBoxModel model={.font=TextBoxFontText,.text=text,.text_on_screen=furi_string_alloc(),.text_line=furi_string_alloc()};
 text_box_view_draw_callback(c,&model);
 if(page){model.scroll_pos=MIN(page,MAX(0,model.scroll_num-1));text_box_view_draw_callback(c,&model);}
 save(c,dir,name);
 // Exercise scrolling in both directions, not only the first screenshot.
 for(int i=0;i<model.scroll_num;i++){model.scroll_pos=i;text_box_view_draw_callback(c,&model);}
 for(int i=model.scroll_num-1;i>=0;i--){model.scroll_pos=i;text_box_view_draw_callback(c,&model);}
 furi_string_free(model.text_on_screen);furi_string_free(model.text_line);
}
int main(int argc,char** argv) {
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&c,FontSecondary);canvas_set_color(&c,ColorBlack);
 const char* main_items[]={"Open file A","Inspect A","Open file B","Inspect B","Compare fields","Export comparison","About / limits"};
 menu(&c,argv[1],"01-inspector","Capture Inspector",main_items,7,0,0);
 menu(&c,argv[1],"02-inspector-bottom","Capture Inspector",main_items,7,5,3);
 const char* differences[]={"Summary","[=] Filetype #1","[=] Version #1","[~] Frequency #1","[=] Protocol #1","[+] Unknown field #1"};
 menu(&c,argv[1],"03-compare","Compare fields",differences,6,3,2);
 text_page(&c,argv[1],"04-field","Frequency #1\n\nA: 433920000\n\nB: 433925000\n\nStored values only.",0);
 text_page(&c,argv[1],"05-summary","Stored field comparison\nSame: 7\nChanged: 1\nOnly A: 0\nOnly B: 1\n\nNo radio or CRC verification.\nField order is ignored; repeated names match by occurrence.",0);
 text_page(&c,argv[1],"06-long-value","Unknown field\n\nABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n\nStored value; not verified.",2);
 const char* status_items[]={"Module metadata","Protocol Pack files","RF Capabilities","About"};
 menu(&c,argv[1],"07-status","ARF Status",status_items,4,0,0);
 const char* report="Manifest/API check only\nHash: not verified\nNo trusted hash reference.\nImports/run: not tested.\nHardware: not tested.\n\nProtoPirate\nHeader compatible\nAPI 88.9; F7; v3.2\n123456 bytes\n\nCapture Inspector\nMissing file\n\nExample package\nAPI too new\n";
 text_page(&c,argv[1],"08-status-scope",report,0);
 text_page(&c,argv[1],"09-status-file",report,6);
 text_page(&c,argv[1],"10-read-error","Storage read error",0);
 text_page(&c,argv[1],"11-limit","Capture limit exceeded",0);
 text_page(&c,argv[1],"12-cleanup-error","Save failed. SD unavailable.\nAn incomplete report may remain.",0);
 return 0;
}
''')
    return source + "\n".join(additions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="inspector-native-") as directory:
        temp = Path(directory)
        src = temp / "render.c"
        src.write_text(build_source())
        sources = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", *flags, "-ffunction-sections", gc,
                        "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"), str(src),
                        *map(str, sources), "-lm", "-o", str(temp / "render")], check=True)
        subprocess.run([str(temp / "render"), str(args.output.resolve())], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    frames = sorted(args.output.glob("*.pgm"))
    assert len(frames) == 12
    sheet = Image.new("RGB", (3 * 404, 4 * 232), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(frames):
        with Image.open(path) as image:
            assert image.size == (128, 64)
            png = path.with_suffix(".png")
            image.save(png)
            x, y = (i % 3) * 404 + 10, (i // 3) * 232 + 28
            sheet.paste(image.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
            draw.text((x, y - 20), path.stem, fill="black")
    sheet.save(args.output / "contact-sheet.png")
    (args.output / "report.json").write_text(json.dumps({"frames": len(frames), "native_components": ["Submenu", "TextBox"], "hardware_test": False}, indent=2))
    print(args.output / "contact-sheet.png")


if __name__ == "__main__":
    main()
