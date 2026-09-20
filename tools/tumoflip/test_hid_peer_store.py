"""Selection preferences must not replace the existing HID pairing/key store."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/hid_app"


def stripped(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma")))


class HidPeerStoreTests(unittest.TestCase):
    def test_preferences_are_journalled_and_validated(self):
        path = APP / "helpers/hid_peer_store.c"
        self.assertTrue(path.exists(), "HID peer preference store is missing")
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define GAP_BONDED_DEVICES_MAX 35
#define APP_DATA_PATH(x) x
typedef struct {uint8_t address_type;uint8_t address[6];} GapBondedDevice;
typedef int Storage;
typedef struct{int slot;bool open;} File;
typedef struct{uint64_t size;} FileInfo;
enum{FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL,FSAM_READ,FSAM_WRITE,FSOM_OPEN_EXISTING,FSOM_CREATE_ALWAYS};
static unsigned char disk[2][2048];static size_t sizes[2];static int failure;
static int slot(const char*p){assert(!strstr(p,"keys")&&!strstr(p,".cfg"));return p[strlen(p)-1]=='b';}
static int storage_common_stat(Storage*s,const char*p,FileInfo*i){(void)s;i->size=sizes[slot(p)];return i->size?FSE_OK:FSE_NOT_EXIST;}
static File*storage_file_alloc(Storage*s){(void)s;return calloc(1,sizeof(File));}
static void storage_file_free(File*f){assert(!f->open);free(f);}
static bool storage_file_open(File*f,const char*p,int mode,int how){
 f->slot=slot(p);if(failure==1)return false;
 if(mode==FSAM_WRITE){assert(how==FSOM_CREATE_ALWAYS);sizes[f->slot]=0;}
 f->open=true;return true;
}
static size_t storage_file_read(File*f,void*out,size_t n){if(n>sizes[f->slot])n=sizes[f->slot];memcpy(out,disk[f->slot],n);return n;}
static size_t storage_file_write(File*f,const void*p,size_t n){if(failure==2)n--;memcpy(disk[f->slot],p,n);sizes[f->slot]=n;return n;}
static bool storage_file_sync(File*f){(void)f;return failure!=3;}
static bool storage_file_close(File*f){f->open=false;return failure!=4;}
static uint32_t crc32_calc_buffer(uint32_t crc,const void*p,size_t n){const uint8_t*b=p;while(n--)crc=crc*33+*b++;return crc;}
''' + stripped(APP / "helpers/hid_peer_store.h") + stripped(path) + r'''
int main(void){
 HidPeerStore s;assert(hid_peer_store_load(NULL,&s));assert(!s.preferences.selected);
 HidPeerPreferences p={0};p.selected=1;p.peer.address[0]=42;p.count=1;p.labels[0].peer=p.peer;
 strcpy(p.labels[0].name,"My Mac");assert(hid_peer_store_save(NULL,&s,&p));
 unsigned active=s.slot;unsigned char original[2048];memcpy(original,disk[active],2048);
 for(failure=1;failure<=4;failure++){
  p.peer.address[0]=7;assert(!hid_peer_store_save(NULL,&s,&p));
  assert(s.preferences.peer.address[0]==42&&!memcmp(original,disk[active],2048));
 }
 failure=0;p.peer.address[0]=7;assert(hid_peer_store_save(NULL,&s,&p));
 HidPeerStore restored;assert(hid_peer_store_load(NULL,&restored));assert(restored.preferences.peer.address[0]==7);
 p.count=36;assert(!hid_peer_store_save(NULL,&s,&p));p.count=1;
 memset(p.labels[0].name,'x',sizeof(p.labels[0].name));assert(!hid_peer_store_save(NULL,&s,&p));
 strcpy(p.labels[0].name,"bad\nname");assert(!hid_peer_store_save(NULL,&s,&p));
 disk[0][0]^=1;disk[1][0]^=1;assert(!hid_peer_store_load(NULL,&restored));
 assert(!restored.preferences.selected);return 0;
}
''')

    def test_remote_starts_in_device_picker_without_open_advertising(self):
        source = (APP / "hid.c").read_text()
        body = source[source.index("int32_t hid_ble_app("):]
        self.assertIn("bt_profile_start_idle", body)
        self.assertNotIn("furi_hal_bt_start_advertising", body)
        self.assertIn("HidSceneDevices", body)


if __name__ == "__main__":
    unittest.main()
