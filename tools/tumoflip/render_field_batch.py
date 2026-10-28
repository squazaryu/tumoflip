"""Render actual field-batch draw callbacks, stock GUI and firmware fonts.

Fixtures are synthetic; this never opens a device, coil, UART or PWM output.
Text bounds/overlap include long values, all custom pages and error states.
"""
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
APP = ROOT / 'applications/system/device_library'
SERVO = ROOT / 'applications_user/servo_tester_plus'


def declaration(path):
    return re.sub(r'^#(?:include|pragma).*\n', '\n', path.read_text(), flags=re.M)


def first_model(source):
    start = source.index('typedef struct')
    return source[start:source.index(';', source.index('}', start)) + 1] + '\n'


def build_source():
    result = radio.build_source().split('int main(int argc,char**argv)', 1)[0]
    for name in ('power_history.h', 'sensor_cards.h', 'sensor_cards.c', 'varta_inspect.h'):
        result += declaration(APP / name)
    result += declaration(SERVO / 'servo_model.h')
    hardware = (SERVO / 'servo_hardware.h').read_text()
    result += hardware[hardware.index('typedef enum'):hardware.index('typedef struct')]
    sources = [(SERVO / 'servo_tester_plus.c', 'servo_draw'),
               (APP / 'power_journal.c', 'power_journal_draw'),
               (APP / 'uhf_app.c', 'uhf_app_draw'), (APP / 'sensor_app.c', 'sensor_app_draw'),
               (APP / 'varta_app.c', 'varta_app_draw')]
    for path, _ in sources:
        result += first_model(path.read_text())
    result += r'''
static struct{int left,right,top,bottom;}batch_spans[32];static unsigned batch_count;
static void batch_bounds(Canvas*c,int x,int y,const char*t){
 int width=canvas_string_width(c,t),top=y-u8g2_GetAscent(&c->fb),bottom=y-u8g2_GetDescent(&c->fb);
 if(!width)return;
 if(x<0||x+width>128||top<0||bottom>53){fprintf(stderr,"Bad field-batch bounds: %s x%d..%d y%d..%d\n",t,x,x+width,top,bottom);abort();}
 for(unsigned i=0;i<batch_count;i++)if(x<batch_spans[i].right+1&&x+width+1>batch_spans[i].left&&
    top<batch_spans[i].bottom&&bottom>batch_spans[i].top){fprintf(stderr,"Overlapping field-batch text: %s\n",t);abort();}
 assert(batch_count<32);batch_spans[batch_count++]=(typeof(batch_spans[0])){x,x+width,top,bottom};
}
static void batch_str(Canvas*c,int x,int y,const char*t){batch_bounds(c,x,y,t);canvas_draw_str(c,x,y,t);}
static void batch_aligned(Canvas*c,int x,int y,Align h,Align v,const char*t){int w=canvas_string_width(c,t);
 int left=x-(h==AlignCenter?w/2:h==AlignRight?w:0);int baseline=y+(v==AlignTop?u8g2_GetAscent(&c->fb):v==AlignCenter?u8g2_GetAscent(&c->fb)/2:0);
 batch_bounds(c,left,baseline,t);canvas_draw_str_aligned(c,x,y,h,v,t);}
#define canvas_draw_str batch_str
#define canvas_draw_str_aligned batch_aligned
'''
    for path, name in sources:
        result += function(path.read_text(), 'static void ' + name + '(')
    result += '\n#undef canvas_draw_str\n#undef canvas_draw_str_aligned\n'
    result += r'''
int main(int argc,char**argv){assert(argc==2);Canvas c={0};screen=&c;static uint8_t buf[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&c.fb,buf,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);canvas_set_color(&c,ColorBlack);
 ServoViewModel servo={.model={.minimum_us=1000,.maximum_us=2000,.pulse_us=1500}};
 batch_count=0;servo_draw(&c,&servo);save(&c,argv[1],"01-servo-off");
 servo.model.armed=true;servo.model.mode=ServoSweep;servo.gpio_power=true;
 batch_count=0;servo_draw(&c,&servo);save(&c,argv[1],"02-servo-sweep");
 servo.model.armed=false;servo.status=ServoHardwareBusy;batch_count=0;servo_draw(&c,&servo);save(&c,argv[1],"03-servo-busy");
 servo.status=ServoHardwarePowerError;servo.model.minimum_us=500;servo.model.maximum_us=2500;servo.model.pulse_us=2500;
 batch_count=0;servo_draw(&c,&servo);save(&c,argv[1],"04-servo-power-error");
 PowerJournalView power={.sample={.soc=90,.voltage_mv=3850,.current_ma=-100,.temperature_tenths=251,.gauge_health=95,.heap_free=32768},.sample_ok=true,.writable=true,.store_ok=true};
 batch_count=0;power_journal_draw(&c,&power);save(&c,argv[1],"05-power-live");
 power.paused=true;power.sample.soc=100;power.sample.current_ma=-5000;power.sample.temperature_tenths=-200;power.sample.heap_free=262144;
 batch_count=0;power_journal_draw(&c,&power);save(&c,argv[1],"06-power-long-values");
 power.page=1;power.graph_count=64;for(unsigned i=0;i<64;i++)power.graph[i]=100-i;
 batch_count=0;power_journal_draw(&c,&power);save(&c,argv[1],"07-power-graph");
 power.page=2;power.estimate=true;power.capacity=1200;power.segments=16;power.rejected=UINT32_MAX;
 batch_count=0;power_journal_draw(&c,&power);save(&c,argv[1],"08-power-evidence");
 power.estimate=false;power.writable=false;batch_count=0;power_journal_draw(&c,&power);save(&c,argv[1],"09-power-readonly");
 UhfViewModel uhf={0};batch_count=0;uhf_app_draw(&c,&uhf);save(&c,argv[1],"10-uhf-no-reader");
 uhf.connected=true;batch_count=0;uhf_app_draw(&c,&uhf);save(&c,argv[1],"11-uhf-probe");
 uhf.identified=true;uhf.version[0]=255;uhf.version[1]=255;uhf.version[2]=255;uhf.running=true;uhf.count=32;uhf.errors=UINT32_MAX;uhf.full=UINT32_MAX;
 batch_count=0;uhf_app_draw(&c,&uhf);save(&c,argv[1],"12-uhf-inventory");
 uhf.running=false;uhf.timeout=true;batch_count=0;uhf_app_draw(&c,&uhf);save(&c,argv[1],"13-uhf-timeout");
 SensorViewModel sensor={.writable=true,.saved=true};batch_count=0;sensor_app_draw(&c,&sensor);save(&c,argv[1],"14-sensor-empty");
 strcpy(sensor.card.protocol,"LaCrosse_TX141THBv2");sensor.count=8;sensor.index=7;sensor.card.id=0xFFFFFFFF;sensor.card.channel=255;sensor.card.count=32;sensor.card.head=0;sensor.listening=true;
 for(unsigned i=0;i<32;i++)sensor.card.history[i]=(SensorPoint){.timestamp=100+i,.temperature=-198+i,.humidity=100,.fields=7};sensor.now=191;
 batch_count=0;sensor_app_draw(&c,&sensor);save(&c,argv[1],"15-sensor-card");
 sensor.graph=true;batch_count=0;sensor_app_draw(&c,&sensor);save(&c,argv[1],"16-sensor-history");
 sensor.graph=false;sensor.card.channel=254;sensor.now=UINT32_MAX;sensor.card.history[31].fields=0;sensor.writable=false;
 batch_count=0;sensor_app_draw(&c,&sensor);save(&c,argv[1],"17-sensor-long-values");
 VartaViewModel varta={.reading=true,.seconds=15};batch_count=0;varta_app_draw(&c,&varta);save(&c,argv[1],"18-varta-reading");
 varta.reading=false;varta.ready=true;varta.result=(VartaInspection){.id={3,0,0x12,0x34,0x56},.known_prefix=true};
 batch_count=0;varta_app_draw(&c,&varta);save(&c,argv[1],"19-varta-observation");
 varta.result.known_prefix=false;batch_count=0;varta_app_draw(&c,&varta);save(&c,argv[1],"20-varta-unusual");
 varta.ready=false;batch_count=0;varta_app_draw(&c,&varta);save(&c,argv[1],"21-varta-timeout");
'''
    # Exact production help text; exercise every scroll position using stock TextBox.
    helps = [(SERVO / 'servo_tester_plus.c', r'Servo Tester+\n\nPA7'),
             (APP / 'power_journal.c', 'APP-ONLY history'), (APP / 'uhf_app.c', 'UCM601 inventory only'),
             (APP / 'sensor_app.c', 'Sensor Watch 1.0'), (APP / 'varta_app.c', 'Varta Inspector 1.0')]
    for index, (path, marker) in enumerate(helps, 22):
        text = path.read_text()
        result += f'text_page(&c,argv[1],"{index:02d}-scope",{radio.message(text, marker)},0);\n'
        result += f'text_page(&c,argv[1],"{index+5:02d}-scope-bottom",{radio.message(text, marker)},1000);\n'
    result += 'return 0;}\n'
    return result


def render(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='field-batch-ui-') as folder:
        work = Path(folder)
        (work / 'render.c').write_text(build_source())
        sources = [p for p in (ROOT / 'lib/u8g2').glob('*.c') if p.name != 'u8g2_glue.c']
        built = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-w', '-fsanitize=address,undefined',
            '-fno-sanitize-recover=all', '-ffunction-sections', '-Wl,-dead_strip', '-I', str(ROOT / 'lib/u8g2'),
            '-I', str(ROOT / 'lib/mlib'), str(work / 'render.c'), *map(str, sources), '-lm', '-o', str(work / 'render')], capture_output=True, text=True)
        if built.returncode: raise RuntimeError(built.stderr)
        subprocess.run([str(work / 'render'), str(output.resolve())], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
    frames = sorted(output.glob('*.pgm'))
    if len(frames) != 31: raise RuntimeError(f'Expected 31 field-batch frames, got {len(frames)}')
    sheet = Image.new('RGB', (3 * 404, ((len(frames) + 2) // 3) * 224), '#ececec')
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(frames):
        frame = Image.open(path).convert('RGB')
        frame.save(path.with_suffix('.png'))
        x, y = index % 3 * 404 + 10, index // 3 * 224 + 24
        draw.text((x, y - 19), path.stem, fill='black')
        sheet.paste(frame.resize((384, 192), Image.Resampling.NEAREST), (x, y))
    sheet.save(output / 'contact-sheet.png')
    (output / 'report.json').write_text(json.dumps({'frames': len(frames), 'hardware_test': False,
        'renderer': 'production callbacks / stock GUI / U8g2 fonts', 'text_bounds_and_overlap': True}, indent=2))
    return len(frames)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    print(f'Rendered {render(parser.parse_args().output)} native field-batch frames')
