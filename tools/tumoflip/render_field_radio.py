"""Production Passive NFC / Band Map views with controlled native fixtures.

Uses actual draw callbacks, stock Submenu/TextBox, U8g2 fonts and decoder output.
No device, radio, DMA, SPI or physical-screen interaction takes place.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw
from tools.tumoflip import render_device_library as library
from tools.tumoflip.render_weather_native import function

ROOT = Path(__file__).resolve().parents[2]
NFC = ROOT / "applications_user/nfc_field_sniffer"
SPECTRUM = ROOT / "applications_user/signal_workbench"


def declarations(path):
    return re.sub(r"^#(?:include|pragma).*\n", "\n", path.read_text(), flags=re.M)


def message(source, starts_with):
    """Copy adjacent C string literals directly from the production message."""
    start = source.index('"' + starts_with)
    match = re.match(r'(?:"(?:[^"\\]|\\.)*"\s*)+', source[start:])
    if not match:
        raise ValueError(starts_with)
    return match[0].strip()


def build_source():
    # Reuse the stock render harness, not the unrelated Sensor Helper model.
    source = library.build_source().split("int main(int argc,char**argv)", 1)[0]
    source = source.replace("TumoSpectrumApp", "UnusedSensorApp")
    source += "\ntypedef SubmenuModel Submenu;\n"
    source += declarations(SPECTRUM / "tumospectrum_band_map.h")
    canvas = (ROOT / "applications/services/gui/canvas.c").read_text()
    source += "typedef enum {CanvasDirectionLeftToRight,CanvasDirectionTopToBottom,CanvasDirectionRightToLeft,CanvasDirectionBottomToTop} CanvasDirection;\n"
    source += function(canvas, "void canvas_draw_triangle(")
    elements = (ROOT / "applications/services/gui/elements.c").read_text()
    # Match fbt's source asset; do not substitute an approximate arrow glyph.
    icon_path = next((ROOT / "assets/icons").rglob("ButtonRight_4x7.png"))
    with Image.open(icon_path) as original:
        icon = original.convert("1")
    width, height = icon.size
    bits = [sum(1 << i for i in range(8) if xb * 8 + i < width and
                icon.getpixel((xb * 8 + i, y)) == 0)
            for y in range(height) for xb in range((width + 7) // 8)]
    source += "static const uint8_t I_ButtonRight_4x7_data[]={" + ",".join(map(str, bits)) + "};\n"
    source += f"static const Icon I_ButtonRight_4x7={{{width},{height},I_ButtonRight_4x7_data}};\n"
    source += function(elements, "void elements_button_right(")
    source += r'''
typedef struct {void*band_map,*view_dispatcher;Submenu*peak_menu;unsigned smart_capture_sample_count;} TumoSpectrumApp;
typedef struct {TumoSpectrumApp*app;} TumoSpectrumBandMapModel;
enum{TumoSpectrumViewBandPeaks};
static TumoSpectrumBandMapSnapshot snapshot;
static const TumoSpectrumBandMapBand band={"433",433050000,434790000};
const TumoSpectrumBandMapBand*tumospectrum_band_map_band(size_t i){assert(i==2);return &band;}
void tumospectrum_band_map_get_snapshot(const TumoSpectrumBandMap*m,TumoSpectrumBandMapSnapshot*s){(void)m;*s=snapshot;}
static void tumospectrum_peak_selected(void*c,uint32_t i){(void)c;(void)i;}
static struct{int left,right,y;char text[32];}spans[6];static unsigned span_count;
static void span(Canvas*c,int left,int y,const char*text){if(y>18)return;int right=left+canvas_string_width(c,text);assert(left>=0&&right<=128);
 for(unsigned i=0;i<span_count;i++)if(spans[i].y==y&&left<spans[i].right+2&&right+2>spans[i].left){fprintf(stderr,"Text overlap y%d: %s [%d,%d) / %s [%d,%d)\n",y,spans[i].text,spans[i].left,spans[i].right,text,left,right);abort();}
 assert(span_count<6);spans[span_count].left=left;spans[span_count].right=right;spans[span_count].y=y;snprintf(spans[span_count++].text,32,"%s",text);}
static void checked_str(Canvas*c,int x,int y,const char*t){span(c,x,y,t);canvas_draw_str(c,x,y,t);}
static void checked_aligned(Canvas*c,int x,int y,Align h,Align v,const char*t){int w=canvas_string_width(c,t);span(c,x-(h==AlignCenter?w/2:h==AlignRight?w:0),y,t);canvas_draw_str_aligned(c,x,y,h,v,t);}
#define canvas_draw_str checked_str
#define canvas_draw_str_aligned checked_aligned
'''
    workbench = (SPECTRUM / "signal_workbench.c").read_text()
    for signature in ("static uint8_t tumospectrum_band_map_rssi_y(",
                      "static const char* tumospectrum_band_map_status_name(",
                      "static void tumospectrum_band_map_draw(",
                      "static void tumospectrum_open_peak_history("):
        source += function(workbench, signature)
    source += "\n#undef canvas_draw_str\n#undef canvas_draw_str_aligned\n"
    nfc = (NFC / "nfc_field_sniffer.c").read_text()
    source += r'''
typedef struct {Submenu*menu;bool ready;void*worker,*dispatcher;bool showing_text;} NfcFieldApp;
enum{NfcFieldMenuView,NfcFieldTextView,NfcFieldStart,NfcFieldView,NfcFieldSave,NfcFieldAbout};
static void nfc_field_action(void*c,uint32_t i){(void)c;(void)i;}
'''
    source += function(nfc, "static void nfc_field_menu(NfcFieldApp* app) {")
    for name in ("nfc_field_decoder.h", "nfc_field_decoder.c", "nfc_field_format.c"):
        source += declarations(NFC / name)
    messages = (
        ("04-no-field", "No external RF field."),
        ("05-busy-owner", "NFC / DMA / TIM2 busy."),
        ("06-capture-error", "Capture failed."),
        ("07-save-error", "Save failed."),
        ("08-about", "Tumo Passive NFC 1.0"),
    )
    source += r'''
int main(int argc,char**argv){assert(argc==2);Canvas c={0};screen=&c;static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);canvas_set_color(&c,ColorBlack);
 Submenu menu={0};SubmenuItemArray_init(menu.items);menu.header=furi_string_alloc();
 NfcFieldApp nfc={.menu=&menu};nfc_field_menu(&nfc);render_menu(&c,argv[1],"01-nfc-home",&menu,0,0);
 nfc.ready=true;nfc_field_menu(&nfc);render_menu(&c,argv[1],"02-nfc-ready",&menu,2,0);
 nfc.worker=&nfc;nfc_field_menu(&nfc);render_menu(&c,argv[1],"03-nfc-capturing",&menu,0,0);
'''
    for name, marker in messages:
        source += f' text_page(&c,argv[1],"{name}",{message(nfc, marker)},0);\n'
    source += f' text_page(&c,argv[1],"09-about-scroll",{message(nfc, "Tumo Passive NFC 1.0")},16);\n'
    source += r'''
 char trace[4096];nfc_field_decoder_reset(true);nfc_field_decoder_finish();assert(nfc_field_decoder_format(trace,sizeof(trace)));
 text_page(&c,argv[1],"10-empty-trace",trace,0);
 uint8_t samples[320];memset(samples,16,sizeof(samples));
 for(unsigned k=0;k<8;k++){unsigned bit=k?((0x26>>(k-1))&1):0;unsigned center=(2324*k+(bit?1743:581)+50)/100;for(unsigned i=center-3;i<center+3;i++)samples[i]=0;}
 assert(nfc_field_decoder_feed(samples,sizeof(samples)));nfc_field_decoder_discontinuity(true);nfc_field_decoder_finish();assert(nfc_field_decoder_format(trace,sizeof(trace)));
 text_page(&c,argv[1],"11-partial-trace",trace,0);text_page(&c,argv[1],"12-partial-scroll",trace,5);
 TumoSpectrumApp app={.peak_menu=&menu,.smart_capture_sample_count=3};TumoSpectrumBandMapModel model={.app=&app};
 snapshot=(TumoSpectrumBandMapSnapshot){.status=TumoSpectrumBandMapStatusScanning,.band_index=2,.zoom=1,.noise_floor_dbm=-103,.selected_rssi_dbm=-43,.selected_frequency_hz=433920000,.selected_bin=32};
 for(unsigned i=0;i<64;i++){snapshot.bins[i]=(i>29&&i<35)?-43:-106+i%5;snapshot.peaks[i]=snapshot.bins[i]+2;for(unsigned y=0;y<8;y++)snapshot.history[y][i]=(i>29-y/2&&i<35+y/2)?-43:-106;}
 span_count=0;tumospectrum_band_map_draw(&c,&model);save(&c,argv[1],"13-map-internal");
 snapshot.radio=TumoSpectrumBandMapRadioDual;span_count=0;tumospectrum_band_map_draw(&c,&model);save(&c,argv[1],"14-map-dual");
 snapshot.status=TumoSpectrumBandMapStatusBusy;span_count=0;tumospectrum_band_map_draw(&c,&model);save(&c,argv[1],"15-map-busy");
 snapshot.status=TumoSpectrumBandMapStatusError;span_count=0;tumospectrum_band_map_draw(&c,&model);save(&c,argv[1],"16-map-error");
 tumospectrum_open_peak_history(&app);render_menu(&c,argv[1],"17-no-peaks",&menu,0,0);
 snapshot.peak_count=16;for(unsigned i=0;i<16;i++)snapshot.observed_peaks[i]=(TumoSpectrumBandPeak){.frequency_hz=433920000+i*200000,.rssi_dbm=-35-(int)i};
 tumospectrum_open_peak_history(&app);render_menu(&c,argv[1],"18-peak-history",&menu,0,0);render_menu(&c,argv[1],"19-peaks-bottom",&menu,16,14);
 snapshot.zoom=4;snapshot.selected_rssi_dbm=-127;snapshot.noise_floor_dbm=-127;span_count=0;tumospectrum_band_map_draw(&c,&model);save(&c,argv[1],"20-map-long-values");
 submenu_reset(&menu);SubmenuItemArray_clear(menu.items);furi_string_free(menu.header);return 0;}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="field-radio-ui-") as directory:
        work = Path(directory)
        (work / "render.c").write_text(build_source())
        sources = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        built = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
            "-ffunction-sections", "-Wl,-dead_strip", "-I", str(ROOT / "lib/u8g2"),
            "-I", str(ROOT / "lib/mlib"), str(work / "render.c"), *map(str, sources),
            "-lm", "-o", str(work / "render")], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stderr)
        subprocess.run([str(work / "render"), str(args.output.resolve())], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    frames = sorted(args.output.glob("*.pgm"))
    if len(frames) != 20:
        raise RuntimeError(f"Expected 20 frames, got {len(frames)}")
    sheet = Image.new("RGB", (3 * 404, ((len(frames) + 2) // 3) * 224), "#ececec")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        frame = Image.open(path).convert("RGB")
        frame.save(path.with_suffix(".png"))
        x, y = index % 3 * 404 + 10, index // 3 * 224 + 24
        draw.text((x, y - 19), path.stem, fill="black")
        sheet.paste(frame.resize((384, 192), Image.Resampling.NEAREST), (x, y))
    sheet.save(args.output / "contact-sheet.png")
    (args.output / "report.json").write_text(json.dumps({
        "frames": len(frames), "hardware_test": False,
        "renderer": "production draw callbacks / stock GUI / synthetic PB4 samples",
    }, indent=2))
    print(f"Rendered {len(frames)} production-view frames")


if __name__ == "__main__":
    main()
