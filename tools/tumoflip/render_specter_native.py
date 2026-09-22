"""Render Specter production draw callbacks with firmware U8g2 fonts.

Host fixtures exercise view states; they do not simulate the NFC detector.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as gui

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/specter"


def strip_includes(source):
    return re.sub(r"^#(?:include|pragma).*\n", "", source, flags=re.M)


def support():
    canvas = (ROOT / "applications/services/gui/canvas.c").read_text()
    parts = [gui.SUPPORT,
             canvas[canvas.index("const CanvasFontParameters canvas_font_params"):
                    canvas.index("Canvas* canvas_init")]]
    for signature in (
        "void canvas_clear(", "void canvas_set_color(", "void canvas_set_font(",
        "void canvas_draw_str(", "void canvas_draw_str_aligned(",
        "uint16_t canvas_string_width(", "void canvas_draw_dot(",
        "void canvas_draw_box(", "void canvas_draw_frame(", "void canvas_draw_line(",
        "void canvas_draw_circle(", "void canvas_draw_disc(",
    ):
        parts.append(gui.function(canvas, signature))
    parts += ['#define SPECTER_HISTORY_LEN 64u\n#define SPECTER_TRACE_LEN 128u\n#define WATCH_NO_TIME UINT32_MAX',
              strip_includes((APP / "views/view_chrome.h").read_text()),
              gui.function(gui.DRIVER, "static void save(")]
    return "\n".join(parts)


FIXTURES = {
    "survey": r"""
 SurveyModel m={.total_ms=30000,.elapsed_ms=1000};
 survey_view_draw(&c,&m);save(&c,argv[1],"survey-running");
 m.finished=true;m.summary.elapsed_ms=1000;m.verdict=survey_verdict(&m.summary);
 canvas_clear(&c);survey_view_draw(&c,&m);save(&c,argv[1],"survey-too-short");
 m.summary.elapsed_ms=30000;m.verdict=survey_verdict(&m.summary);
 canvas_clear(&c);survey_view_draw(&c,&m);save(&c,argv[1],"survey-clean");
 m.summary.contacts=1;m.summary.peak_ref=100;m.verdict=survey_verdict(&m.summary);
 canvas_clear(&c);survey_view_draw(&c,&m);save(&c,argv[1],"survey-active");
 m.error=true;canvas_clear(&c);survey_view_draw(&c,&m);save(&c,argv[1],"survey-error");
""",
    "fingerprint": r"""
 FingerprintModel m={.armed=true,.present=true,
 .cadence={.bursts=8,.burst_ms=12,.gap_ms=28,.period_ms=40,.jitter_ms=2,.duty=30}};
 m.verdict=emitter_classify(&m.cadence);
 for(unsigned i=0;i<SPECTER_TRACE_LEN;i++)m.trace[i]=(i%10<3)?3:0;
 fingerprint_view_draw(&c,&m);save(&c,argv[1],"fingerprint-polling");
 m.verdict.klass=EmitterClassIntermittent;
 canvas_clear(&c);fingerprint_view_draw(&c,&m);save(&c,argv[1],"fingerprint-intermittent");
 m.cadence=(CadenceStats){0};m.verdict=emitter_classify(&m.cadence);m.present=false;
 canvas_clear(&c);fingerprint_view_draw(&c,&m);save(&c,argv[1],"fingerprint-empty");
 m.error=true;canvas_clear(&c);fingerprint_view_draw(&c,&m);save(&c,argv[1],"fingerprint-error");
""",
    "sweep": r"""
 SweepModel m={.armed=true,.present=true,.strength=100,.peak=100,
 .threshold_shown=30,.calibrating=true,.calib_progress=50,.anim=2};
 strcpy(m.sens,"Medium");
 sweep_view_draw(&c,&m);save(&c,argv[1],"sweep-calibration");
 m.calibrating=false;canvas_clear(&c);sweep_view_draw(&c,&m);save(&c,argv[1],"sweep-reader");
 m.present=false;m.strength=0;canvas_clear(&c);sweep_view_draw(&c,&m);save(&c,argv[1],"sweep-empty");
 m.error=true;canvas_clear(&c);sweep_view_draw(&c,&m);save(&c,argv[1],"sweep-error");
""",
    "watch": r"""
 WatchModel m={.armed=true,.watching_ms=120000,.last_ms=WATCH_NO_TIME};
 watch_view_draw(&c,&m);save(&c,argv[1],"watch-waiting");
 m.present=true;m.strength=100;m.peak=100;m.contacts=2;m.last_ms=119000;
 canvas_clear(&c);watch_view_draw(&c,&m);save(&c,argv[1],"watch-reader");
 m.error=true;canvas_clear(&c);watch_view_draw(&c,&m);save(&c,argv[1],"watch-error");
""",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    core = support()
    for name, fixture in FIXTURES.items():
        view = (APP / "views" / (name + "_view.c")).read_text()
        view = view[:view.index("static bool " + name + "_view_input(")]
        view = re.sub(r"struct \w+View \{.*?\};", "", view, flags=re.S)
        includes = '\n'.join('#include "' + str(APP / "helpers" / h) + '"'
                             for h in ("emitter_classify.h", "survey_verdict.h", "field_scale.h"))
        driver = r"""
int main(int argc,char** argv) {
 assert(argc==2);Canvas c={0};static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&c,FontSecondary);canvas_set_color(&c,ColorBlack);
""" + fixture + "\nreturn 0;}"
        with tempfile.TemporaryDirectory(prefix="specter-native-") as directory:
            tmp = Path(directory)
            source = tmp / "render.c"
            source.write_text(core + "\n" + includes + "\n" + strip_includes(view) + driver)
            library = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
            helpers = [APP / "helpers" / (s + ".c") for s in ("emitter_classify", "survey_verdict", "field_scale")]
            gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
            subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g",
                            "-fsanitize=address,undefined", "-ffunction-sections", gc,
                            "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                            str(source), *map(str, library + helpers), "-lm", "-o", str(tmp / "render")], check=True)
            subprocess.run([str(tmp / "render"), str(args.output.resolve())], check=True)
    frames = sorted(args.output.glob("*.pgm"))
    sheet = Image.new("RGB", (4 * 404, ((len(frames) + 3) // 4) * 224), "#eeeeee")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(frames):
        with Image.open(path) as frame:
            frame.save(path.with_suffix(".png"))
            x, y = (i % 4) * 404 + 8, (i // 4) * 224 + 24
            sheet.paste(frame.resize((384,192), Image.Resampling.NEAREST).convert("RGB"), (x,y))
            draw.text((x,y-20), path.stem, fill="black")
    sheet.save(args.output / "contact-sheet.png")
    print(f"Rendered {len(frames)} production view states")


if __name__ == "__main__":
    main()
