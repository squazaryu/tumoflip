"""Rasterize real NFC delete scene and CD-ROM draw callback with stock U8g2."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from tools.tumoflip import render_weather_native as renderer

ROOT = Path(__file__).resolve().parents[2]


def main():
    from PIL import Image, ImageDraw
    output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
    cd = (ROOT / 'applications_user/usb_cdrom/views/cd_view.c').read_text()
    chunks = r'''
#define DISC_X 19
#define DISC_Y 31
#define DISC_RADIUS 14
#define TEXT_X 38
#define furi_string_cat_str furi_string_cat
typedef struct {FuriString *name,*info,*status_string;bool loaded;uint32_t read_speed,bytes_read,update_time;uint8_t spin;bool connection_error;} CdViewModel;
static const int8_t spin_dx[]={0,7,10,7,0,-7,-10,-7};
static const int8_t spin_dy[]={-10,-7,0,7,10,7,0,-7};
'''
    for signature in ('static void append_suffixed_byte_count(', 'static void draw_disc(', 'static void draw_eject(', 'static void cd_view_draw_callback('):
        chunks += renderer.function(cd, signature) + '\n'
    chunks += r'''
enum {GuiButtonTypeLeft,GuiButtonTypeCenter,GuiButtonTypeRight,NfcViewWidget=0,NfcDeviceNameTypeFull=0};
typedef Canvas Widget;
typedef struct {Canvas*widget;FuriString*file_name;void*nfc_device,*view_dispatcher;}NfcApp;
static void nfc_scene_delete_widget_callback(void){}
static const uint8_t*nfc_device_get_uid(void*c,size_t*n){(void)c;static const uint8_t u[]={4,0xab,0xcd,0xef,0x12,0x34,0x56,0x78,0x90,0xab};*n=10;return u;}
static const char*nfc_device_get_name(void*c,int t){(void)c;(void)t;return "MIFARE DESFire EV3";}
static void view_dispatcher_switch_to_view(void*c,int v){(void)c;(void)v;}
static void widget_add_text_box_element(Canvas*c,int x,int y,int w,int h,Align a,Align b,const char*t,bool dots){elements_text_box(c,x,y,w,h,a,b,t,dots);}
static void widget_add_button_element(Canvas*c,int key,const char*t,void*cb,void*ctx){(void)cb;(void)ctx;if(key==GuiButtonTypeLeft)elements_button_left(c,t);else if(key==GuiButtonTypeRight)elements_button_right(c,t);}
static void widget_add_string_element(Canvas*c,int x,int y,Align a,Align b,Font f,const char*t){canvas_set_font(c,f);canvas_draw_str_aligned(c,x,y,a,b,t);}
'''
    icon_path = next((ROOT/'assets/icons').rglob('ButtonRight_4x7.png'))
    icon = Image.open(icon_path).convert('1')
    bits = [sum((1 << bit) for bit in range(8) if byte * 8 + bit < icon.width and icon.getpixel((byte * 8 + bit, y)) == 0)
            for y in range(icon.height) for byte in range((icon.width + 7)//8)]
    icon_source = 'static const uint8_t right_bits[]={' + ','.join(map(str,bits)) + '};\n'
    icon_source += f'static const Icon I_ButtonRight_4x7={{{icon.width},{icon.height},right_bits}};\n'
    right = renderer.function((ROOT/'applications/services/gui/elements.c').read_text(), 'void elements_button_right(')
    chunks = icon_source + right + '\n' + chunks
    chunks += renderer.function((ROOT/'applications/main/nfc/scenes/nfc_scene_delete.c').read_text(), 'void nfc_scene_delete_on_enter(')
    original = renderer.DRIVER
    prefix = original[:original.index('int main(')]
    initialize = original[original.index('Canvas c={0};'):original.index('    CHECK_WORD_WRAP')]
    renderer.DRIVER = chunks + prefix + r'''
int main(int argc,char**argv){assert(argc==2);
''' + initialize + r'''
 CdViewModel m={.name=furi_string_alloc(),.info=furi_string_alloc(),.status_string=furi_string_alloc()};
 for(int i=0;i<4;i++){canvas_clear(&c);m.loaded=i<2;m.connection_error=i==3;m.bytes_read=UINT32_MAX;m.read_speed=123456;m.spin=3;
  furi_string_set_str(m.name,"Long Windows maintenance image name.iso");furi_string_set_str(m.info,"DVD 3.9G  Locked");
  cd_view_draw_callback(&c,&m);char name[40];snprintf(name,sizeof(name),"usb-cd-state-%d",i);save(&c,argv[1],name);}
 NfcApp n={.widget=&c,.file_name=furi_string_alloc()};
 for(int i=0;i<2;i++){canvas_clear(&c);furi_string_set_str(n.file_name,i?"Own card with a very long saved filename":"Own card");nfc_scene_delete_on_enter(&n);save(&c,argv[1],i?"nfc-delete-long":"nfc-delete-short");}
 return 0;}
'''
    source = renderer.build_source(None)
    with tempfile.TemporaryDirectory(prefix='browser-usb-raster-') as temporary:
        work=Path(temporary); (work/'render.c').write_text(source)
        u8g=[str(p) for p in (ROOT/'lib/u8g2').glob('*.c') if p.name!='u8g2_glue.c']
        gc='-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections'
        subprocess.run(['cc','-std=c11','-O1','-w','-ffunction-sections',gc,'-I',str(ROOT/'lib/u8g2'),'-I',str(ROOT/'lib/mlib'),str(work/'render.c'),*u8g,'-lm','-o',str(work/'render')],check=True)
        subprocess.run([str(work/'render'),str(output.resolve())],check=True)
    images=sorted(output.glob('*.pgm'));sheet=Image.new('RGB',(3*400,2*240),'#e8e8e8');draw=ImageDraw.Draw(sheet)
    for index,path in enumerate(images):
        x=index%3*400+8;y=index//3*240+30
        sheet.paste(Image.open(path).resize((384,192),Image.Resampling.NEAREST),(x,y));draw.text((x,y-20),path.stem,fill='black')
    sheet.save(output/'contact-sheet.png')
    (output/'report.json').write_text(json.dumps({'renderer':'production scene/draw callbacks + stock U8g2','frames':[p.name for p in images],'hardware':False},indent=2)+'\n')
    print(output/'contact-sheet.png')


if __name__=='__main__': main()
