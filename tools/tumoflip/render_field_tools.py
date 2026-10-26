"""Production stock-menu constructors at native128x64, not LCD acceptance."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image, ImageDraw
from tools.tumoflip import render_device_library as library
from tools.tumoflip.render_weather_native import function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


def build_source():
    source = library.build_source().split("int main(int argc,char**argv)", 1)[0]
    source += "\ntypedef SubmenuModel Submenu;\n"
    for name, boundary in (("field_round.h", "bool field_round_begin"),
                           ("field_menu.h", "void field_menu_build")):
        source += re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / name).read_text().split(boundary, 1)[0], flags=re.M)
    source += function((APP / "field_round.c").read_text(), "const char* field_round_result_name(")
    source += function((APP / "field_menu.c").read_text(), "void field_menu_build(")
    source += r'''
int main(int argc,char**argv){assert(argc==2);Canvas c={0};screen=&c;static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);canvas_set_color(&c,ColorBlack);
 Submenu menu={0};SubmenuItemArray_init(menu.items);menu.header=furi_string_alloc();
 FieldRound round={.started=1,.count=4,.items={{.object="Office",.result=0},{.object="Front gate",.result=1},{.object="Weather sensor",.result=2},{.object="Garage",.result=3}}};
 DeviceCard card={.name="Office"};char names[32][32];for(unsigned i=0;i<32;i++)snprintf(names[i],32,"Object %02u",i+1);
 FieldMenuState state={.round=&round,.card=&card,.names=(const char(*)[32])names};
 state.page=FieldPageHome;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"01-home",&menu,0,0);render_menu(&c,argv[1],"02-home-bottom",&menu,7,5);
 state.page=FieldPageNewConfirm;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"03-new-round",&menu,1,0);
 state.page=FieldPageRound;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"04-round",&menu,0,0);render_menu(&c,argv[1],"05-round-bottom",&menu,4,2);
 state.page=FieldPageItem;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"06-check",&menu,2,0);render_menu(&c,argv[1],"07-check-note",&menu,5,3);
 strcpy(round.items[0].object,"WWWWWWWWWWWWWWWWWWWWWWWW");state.page=FieldPageRound;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"08-long-object",&menu,0,0);
 state.page=FieldPageCards;state.count=32;state.more=true;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"09-export",&menu,0,0);render_menu(&c,argv[1],"10-export-bottom",&menu,32,30);
 state.page=FieldPageBundle;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"11-verified-package",&menu,1,0);
 state.page=FieldPageBusy;field_menu_build(&menu,&state,NULL,NULL);render_menu(&c,argv[1],"12-working",&menu,0,0);
 text_page(&c,argv[1],"13-scan-result","Capture comparison\nOK: 128 Changed: 2\nMissing: 1 New: 3\nUnreadable: 0\n\nNo files deleted.\nBaseline unchanged.",0);
 text_page(&c,argv[1],"14-cancelled","Cancelled or failed.\nCheck SD / limits.\nOriginal captures kept.\nIncomplete copies may\nremain in imports/.",0);
 text_page(&c,argv[1],"15-save-error","Result not saved.\nPrevious result retained.",0);
 text_page(&c,argv[1],"16-module-missing","Install Field Tools\nfrom FW Packages.\nOriginal files unchanged.",0);
 text_page(&c,argv[1],"17-private","Object package verified.\nCopy from SD / USB.\nCompanion Files can\nupload a copy back.\nMay contain private data.",0);
 text_page(&c,argv[1],"18-no-auto-pass","Select a result first.\nOpening a file does not\nmark the check as passed.",0);
 char input[96]="Reader responds";TextInputModel note={.header="Result note",.text_buffer=input,.text_buffer_size=sizeof(input),.minimum_length=0,.cursor_pos=15};
 text_input_view_draw_callback(&c,&note);save(&c,argv[1],"19-result-note");
 strcpy(input,"Office copy");note.header="New object name";note.text_buffer_size=25;note.minimum_length=1;note.cursor_pos=11;
 text_input_view_draw_callback(&c,&note);save(&c,argv[1],"20-import-name");
 text_page(&c,argv[1],"21-old-library","Need Device Library 1.2\nand a readable object.\nUpdate FW Packages.\nRound result unchanged.",0);
 return 0;}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="field-tools-ui-") as directory:
        work = Path(directory)
        (work / "render.c").write_text(build_source())
        sources = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        built = subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
                        "-ffunction-sections", "-Wl,-dead_strip", "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                        str(work / "render.c"), *map(str, sources), "-o", str(work / "render")],
                       capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stderr)
        subprocess.run([str(work / "render"), str(args.output)], check=True)
    frames = sorted(args.output.glob("*.pgm"))
    if len(frames) != 21:
        raise RuntimeError(f"Expected 21 frames, got {len(frames)}")
    sheet = Image.new("RGB", (3 * 270, ((len(frames) + 2) // 3) * 155), "#e6e6e6")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        frame = Image.open(path).convert("RGB")
        frame.save(path.with_suffix(".png"))
        x, y = (index % 3) * 270, (index // 3) * 155
        draw.text((x + 5, y + 3), path.stem, fill="black")
        sheet.paste(frame.resize((256, 128), Image.Resampling.NEAREST), (x + 5, y + 20))
    sheet.save(args.output / "contact-sheet.png")
    print(f"Rendered {len(frames)} production-menu frames")


if __name__ == "__main__":
    main()
