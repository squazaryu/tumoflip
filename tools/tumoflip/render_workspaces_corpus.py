"""Render production profile widgets and corpus progress with native GUI components.

128x64 fixtures, repository U8g2/fonts/icons; not a hardware or input acceptance test.
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
from tools.tumoflip import render_inspector_native as inspector
from tools.tumoflip import render_weather_native as gui

ROOT = Path(__file__).resolve().parents[2]


def build_source():
    source = inspector.build_source().split("int main(int argc,char** argv)", 1)[0]
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    extra = gui.function(elements, "void elements_button_right(")
    extra += gui.function(elements, "void elements_progress_bar(")
    # Additional icon comes from the same PNG consumed by fbt, never a redrawn substitute.
    icon = Image.open(next((ROOT / "assets/icons").rglob("ButtonRight_4x7.png"))).convert("1")
    bits = [sum(1 << x for x in range(4) if not icon.getpixel((x, y))) for y in range(7)]
    source += "static const uint8_t right_bits[]={" + ",".join(map(str, bits)) + "};\n"
    source += "static const Icon I_ButtonRight_4x7={4,7,right_bits};\n" + extra
    source += gui.function(gui.DRIVER, "static void text_scroll(")
    source += r'''
typedef struct {Canvas*widget;void*view_dispatcher;} SubGhz;
typedef struct{uint32_t frequency,raw_frequency;char preset[32],raw_preset[32];uint32_t pack,hopping,radio;}WorkspaceProfile;
typedef struct{SubGhz*app;int page;WorkspaceProfile profile;}Workspace;
enum{WorkspacePreview,WorkspaceResult,SubGhzViewIdWidget,GuiButtonTypeRight};
enum{SubGhzRadioDeviceTypeAuto,SubGhzRadioDeviceTypeInternal,SubGhzRadioDeviceTypeExternalCC1101};
static int scroll_position;
static const char*subghz_protocol_pack_group_get_name(unsigned n){static const char*names[]={"Core","Legacy","Kia","Ford","Europe","Asia/US","Alarm","Shuka"};return names[n];}
static void widget_reset(Canvas*c){canvas_clear(c);canvas_set_color(c,ColorBlack);canvas_set_font(c,FontSecondary);}
static void widget_add_text_scroll_element(Canvas*c,int x,int y,int w,int h,const char*t){text_scroll(c,x,y,w,h,t,scroll_position);}
static void widget_add_text_box_element(Canvas*c,int x,int y,int w,int h,Align a,Align b,const char*t,bool dots){elements_text_box(c,x,y,w,h,a,b,t,dots);}
static void workspace_button(void){}
static void widget_add_button_element(Canvas*c,int button,const char*t,void*callback,void*ctx){(void)button;(void)callback;(void)ctx;canvas_set_font(c,FontSecondary);elements_button_right(c,t);}
static void view_dispatcher_switch_to_view(void*d,int v){(void)d;(void)v;}
typedef struct{uint8_t progress;}CorpusProgressModel;
'''
    workspace = (ROOT / "applications/main/subghz/plugins/workspaces/workspaces.c").read_text()
    corpus = (ROOT / "applications_user/tumo_acceptance_suite/corpus_ui.c").read_text()
    source += gui.function(workspace, "static void workspace_preview(")
    source += gui.function(workspace, "static void workspace_message(")
    source += gui.function(corpus, "static void corpus_ui_draw(")
    # Copy user-visible literals from production; fixtures supply data, not alternative wording.
    messages = re.findall(r'workspace_message\(w, ("(?:[^"\\]|\\.)*")\)', workspace)
    # The save branch contains a ternary; include its two production literals explicitly by match.
    for literal in re.findall(r'"(?:[^"\\]|\\.)*"', workspace):
        if literal.startswith(('"Profile saved.', '"Cannot save.', '"Restore failed.', '"Radio unavailable.', '"Protocol Pack incomplete.')):
            messages.append(literal)
    for n, message in enumerate(dict.fromkeys(messages)):
        source += f"static const char*message_{n}={message};\n"
    source += r'''
int main(int argc,char**argv){
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&c,FontSecondary);canvas_set_color(&c,ColorBlack);
 const char*profiles[]={"Save current as...","Load profile"};
 menu(&c,argv[1],"01-profiles","Work Profiles",profiles,2,0,0);
 char name[25]="Weather station";TextInputModel input={.header="New profile name",.text_buffer=name,.text_buffer_size=sizeof(name),.minimum_length=1,.cursor_pos=15};
 widget_reset(&c);text_input_view_draw_callback(&c,&input);save(&c,argv[1],"02-name");
 SubGhz app={.widget=&c};Workspace w={.app=&app,.profile={433920000,315000000,"AM650","FM238",0,0,1}};
 workspace_preview(&w);save(&c,argv[1],"03-profile-details");
 strcpy(w.profile.preset,"VeryLongCustomPresetName");strcpy(w.profile.raw_preset,"SecondLongCustomPresetName");w.profile.pack=5;w.profile.hopping=3;w.profile.radio=2;
 workspace_preview(&w);save(&c,argv[1],"04-profile-long");
 scroll_position=3;workspace_preview(&w);save(&c,argv[1],"05-profile-scrolled");scroll_position=0;
 const char*checks[]={"Add RAW reference","Check all references","Save as reference","Export last report","About / limits"};
 menu(&c,argv[1],"20-references","Decoder References",checks,5,0,0);
 menu(&c,argv[1],"21-references-bottom","Decoder References",checks,5,3,2);
 const char*packs[]={"Core","Legacy","Kia","Ford","Europe","Asia/US","Alarm","Shuka"};
 menu(&c,argv[1],"22-pack","Choose Protocol Pack",packs,8,0,0);
 CorpusProgressModel progress={37};corpus_ui_draw(&c,&progress);save(&c,argv[1],"23-progress");
 progress.progress=100;corpus_ui_draw(&c,&progress);save(&c,argv[1],"24-progress-full");
 text_page(&c,argv[1],"25-candidate","Frames: 24\nProtocols:\nPrinceton\n\nBack > Save as reference\nif this is expected.\nSource file unchanged.",0);
 text_page(&c,argv[1],"26-candidate-bottom","Frames: 24\nProtocols:\nPrinceton\n\nBack > Save as reference\nif this is expected.\nSource file unchanged.",3);
 const char* report="1/3 matched; 2 failed\n\nDecoder checks / schema 1\nFirmware: t-dev-009-008\n\n[00] MATCH\nFrames: 24\n\n[01] Input changed\nFrames: 8\n\n[02] Decoder result changed\nFrames: 12\n\nOffline only.\nRF hardware not tested.";
 text_page(&c,argv[1],"27-report",report,0);
 text_page(&c,argv[1],"28-report-bottom",report,11);
 text_page(&c,argv[1],"29-read-error","RAW read error",0);
 text_page(&c,argv[1],"30-no-frames","No decoded frames",0);
 text_page(&c,argv[1],"31-cancelled","Cancelled\n\nIncomplete run.\nNo reference saved.",0);
 text_page(&c,argv[1],"32-reference-saved","Reference saved:\nreference_00.tref\n\nKeep the original RAW file.\nNo reference replaced.\n\nFolder:\napps_data/\ntumo_acceptance_suite/\nreferences/",0);
'''
    for n, _ in enumerate(dict.fromkeys(messages)):
        source += f'workspace_message(&w,message_{n});save(&c,argv[1],"{6+n:02d}-profile-message-{n+1}");\n'
    return source + "return 0;}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="profiles-corpus-preview-") as directory:
        tmp = Path(directory)
        source = tmp / "render.c"
        source.write_text(build_source())
        library = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined", "-ffunction-sections", gc,
            "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"), str(source), *map(str, library), "-lm", "-o", str(tmp / "render")], check=True)
        subprocess.run([str(tmp / "render"), str(args.output.resolve())], check=True,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    frames = sorted(args.output.glob("*.pgm"))
    for name, subset in (("profiles", [p for p in frames if int(p.stem[:2]) < 20]),
                         ("references", [p for p in frames if int(p.stem[:2]) >= 20])):
        sheet = Image.new("RGB", (3 * 404, ((len(subset) + 2) // 3) * 224), "#ececec")
        draw = ImageDraw.Draw(sheet)
        for i, path in enumerate(subset):
            with Image.open(path) as frame:
                frame.resize((768, 384), Image.Resampling.NEAREST).save(path.with_suffix(".png"))
                x, y = (i % 3) * 404 + 10, (i // 3) * 224 + 24
                sheet.paste(frame.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
                draw.text((x, y - 19), path.stem, fill="black")
        sheet.save(args.output / (name + ".png"))
    overview = Image.new("RGB", (808, 3 * 224), "#ececec")
    draw = ImageDraw.Draw(overview)
    selected = [("01-profiles", "1. Work Profiles"), ("03-profile-details", "2. Review / Apply"),
                ("20-references", "3. Decoder References"), ("23-progress", "4. Offline progress"),
                ("27-report", "5. Results"), ("31-cancelled", "6. Cancelled")]
    for i, (name, label) in enumerate(selected):
        with Image.open(args.output / (name + ".pgm")) as frame:
            x, y = (i % 2) * 404 + 10, (i // 2) * 224 + 24
            overview.paste(frame.resize((384, 192), Image.Resampling.NEAREST).convert("RGB"), (x, y))
            draw.text((x, y - 19), label, fill="black")
    overview.save(args.output / "overview.png")
    (args.output / "report.json").write_text(json.dumps({"frames": len(frames), "hardware_test": False,
        "renderer": "production profile/progress callbacks and native GUI components, U8g2 fonts"}, indent=2))
    print(f"Rendered {len(frames)} states to {args.output}")


if __name__ == "__main__":
    main()
