"""Read-only CD-ROM protocol fixtures; no USB device or user image is accessed."""
import os
from pathlib import Path
import re
import subprocess
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = "applications_user/usb_cdrom"


def source(name):
    ref = os.environ.get("TUMO_USB_CDROM_REF")
    if ref:
        return subprocess.check_output(["git", "show", f"{ref}:non_catalog_apps/usb_cdrom/{name}"], cwd=ROOT, text=True)
    return (ROOT / APP / name).read_text()


class UsbCdromTests(unittest.TestCase):
    def test_usb_profile_is_restored_before_freeing_active_callbacks(self):
        code = source("helpers/cd_usb.c")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#define furi_check assert
typedef struct{void*str_prod_descr,*str_serial_descr;}Interface;
typedef struct{Interface usb;Interface*usb_prev;void*thread;}CdUsb;
static bool locked=true,active=true;
static Interface*current;
static Interface* __attribute__((unused)) furi_hal_usb_get_config(void){return current;}
static void __attribute__((unused)) furi_hal_usb_unlock(void){locked=false;}
static bool furi_hal_usb_set_config(Interface*p,void*c){(void)c;if(locked)return false;current=p;active=false;return true;}
static void furi_thread_free(void*p){(void)p;assert(!active);}
''' + function(code, "void cd_usb_stop(") + r'''
int main(void){Interface prior={0};CdUsb*p=calloc(1,sizeof(*p));p->usb_prev=&prior;current=&p->usb;
 p->usb.str_prod_descr=malloc(1);p->usb.str_serial_descr=malloc(1);cd_usb_stop(p);assert(!locked&&!active&&current==&prior);return 0;}
''')

    def test_big_endian_lba_never_shifts_signed_bytes(self):
        native("#include <stdint.h>\n#include <assert.h>\n" + function(source("helpers/scsi_cd.c"), "static uint32_t get_be32(") + r'''
int main(void){for(unsigned i=0;i<256;i++){uint8_t p[]={(uint8_t)i,0x81,0xfe,0xff};
 assert(get_be32(p)==((uint32_t)i*16777216U+0x81feffU));}return 0;}
''')

    def test_scsi_rejects_writes_bounds_reads_and_handles_malformed_commands(self):
        # Preserve protocol constants, stripping only real include directives.
        header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', source("helpers/scsi_cd.h"), flags=re.M)
        code = re.sub(r'^#include[^\n]*\n', '', source("helpers/scsi_cd.c"), flags=re.M)
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define UNUSED(x) ((void)(x))
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define FURI_LOG_T(...) ((void)0)
''' + header + code + r'''
static unsigned reads;
static uint32_t medium_blocks=32;
static void media(void*c,CdMedia*m){(void)c;*m=(CdMedia){.blocks=medium_blocks,.id=1};}
static bool read_block(void*c,uint32_t lba,uint32_t n,uint8_t*out){(void)c;assert(lba<=32&&n<=32-lba);memset(out,0x5a,n*2048);reads++;return true;}
static void eject(void*c){(void)c;}
static void lock(void*c,bool l){(void)c;(void)l;}
int main(void){CdSession s;CdDevice d={.media=media,.read=read_block,.eject=eject,.lock=lock};uint8_t out[4096],cmd[16]={0};uint32_t len=0;
 scsi_cd_init(&s,d);cmd[0]=0x28;cmd[8]=2;assert(scsi_cd_cmd_start(&s,cmd,10));
 assert(scsi_cd_cmd_tx_data(&s,out,&len,sizeof(out))&&len==4096&&reads==1&&out[0]==0x5a);assert(scsi_cd_cmd_end(&s));
 cmd[0]=0x2a;assert(!scsi_cd_cmd_start(&s,cmd,10));assert(reads==1);
 medium_blocks=2097152;memset(cmd,0,sizeof(cmd));cmd[0]=0xa8;cmd[7]=0x20;
 assert(!scsi_cd_cmd_start(&s,cmd,12));medium_blocks=32;memset(cmd,0,sizeof(cmd));cmd[8]=2;
 cmd[0]=0x28;cmd[2]=0xff;assert(!scsi_cd_cmd_start(&s,cmd,10));assert(reads==1);
 for(unsigned op=0;op<256;op++){for(unsigned n=0;n<=16;n++){
  memset(cmd,0,sizeof(cmd));cmd[0]=op;scsi_cd_init(&s,d);bool ok=scsi_cd_cmd_start(&s,cmd,n);
  if(ok&&s.data_dir==CdDataIn){assert(s.data_len<=sizeof(out));if(s.data_len){len=0;scsi_cd_cmd_tx_data(&s,out,&len,sizeof(out));assert(len<=sizeof(out));}}
 }}return 0;}
''')


if __name__ == '__main__': unittest.main()
