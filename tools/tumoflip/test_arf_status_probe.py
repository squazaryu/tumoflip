"""ARF Status must never label mere file presence as verified readiness."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/arf_tools"


class ArfStatusProbeTests(unittest.TestCase):
    def test_manifest_only_probe_and_explicit_unknown_integrity(self):
        def strip(name):
            return "\n".join(line for line in (APP / name).read_text().splitlines()
                             if not line.startswith(("#include", "#pragma")))
        source = strip("arf_file_probe.c")
        self.assertNotIn("flipper_application_preload", source)
        self.assertNotIn("FSAM_WRITE", source)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int Storage;
enum {FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL,FSE_ALREADY_OPEN,FSAM_READ,FSOM_OPEN_EXISTING};
typedef int FS_Error;
typedef struct {uint64_t size;bool directory;} FileInfo;
static bool file_info_is_dir(const FileInfo* f){return f->directory;}
typedef struct {bool open;} File;
typedef struct {uint16_t api_version_major,api_version_minor;} ElfApiInterface;
static ElfApiInterface api={88,9};
const ElfApiInterface* const firmware_api_interface=&api;
static int allocated,preloads,mode;
static FS_Error storage_common_stat(Storage* s,const char* p,FileInfo* out){
 (void)s;(void)p;if(mode==1)return FSE_NOT_EXIST;if(mode==2)return FSE_INTERNAL;
 out->directory=mode==3;out->size=mode==4?0:(mode==11?17U*1024U*1024U:1024);return FSE_OK;
}
static File* storage_file_alloc(Storage* s){(void)s;allocated++;return calloc(1,sizeof(File));}
static void storage_file_free(File* f){assert(!f->open);allocated--;free(f);}
static bool storage_file_open(File* f,const char* p,int a,int m){(void)p;assert(a==FSAM_READ&&m==FSOM_OPEN_EXISTING);f->open=mode!=13&&mode!=15;return f->open;}
static FS_Error storage_file_get_error(File* f){(void)f;return mode==13?FSE_ALREADY_OPEN:FSE_INTERNAL;}
static bool storage_file_close(File* f){f->open=false;return mode!=14;}
static uint64_t storage_file_size(File* f){assert(f->open);return 1024;}
static bool storage_file_seek(File* f,uint32_t p,bool start){assert(f->open&&p==0&&start);return true;}
static size_t storage_file_read(File* f,void* b,size_t n){assert(f->open);memset(b,0,n);return mode==16?0:n;}
static uint8_t furi_hal_version_get_hw_target(void){return 7;}
''' + strip("arf_elf_metadata.h") + r'''
ArfElfStatus arf_elf_metadata_read(ArfElfRead read_at,void* context,uint64_t size,ArfElfMetadata* m){
 assert(size==1024);preloads++;uint8_t bytes[4];
 if(!read_at(context,0,bytes,sizeof(bytes)))return ArfElfIoError;
 memset(m,0,sizeof(*m));m->api_major=mode==9?87:(mode==10?89:88);m->api_minor=mode==12?10:9;
 m->target=mode==8?18:7;m->app_version=3;memset(m->name,'x',32);
 if(mode==5)return ArfElfInvalid;
 if(mode==6)return ArfElfBadManifest;
 if(mode==7)return ArfElfIoError;
 return ArfElfOk;
}
''' + strip("arf_file_probe.h") + source + r'''
int main(void){
 for(mode=0;mode<=16;mode++){
  preloads=allocated=0;ArfFileProbe out;memset(&out,0x77,sizeof(out));
  arf_file_probe(NULL,"test.fap",&out);
  assert(!allocated && !out.integrity_verified && !out.imports_verified);
  switch(mode){
  case 0:assert(out.status==ArfFileHeaderCompatible&&out.api_major==88&&out.api_minor==9&&out.name[32]==0);break;
  case 1:assert(out.status==ArfFileMissing&&!preloads);break;
  case 2:case 7:case 14:case 15:case 16:assert(out.status==ArfFileIoError);break;
  case 3:assert(out.status==ArfFileNotRegular&&!preloads);break;
  case 4:assert(out.status==ArfFileEmpty&&!preloads);break;
  case 5:assert(out.status==ArfFileInvalid);break;
  case 6:assert(out.status==ArfFileInvalidManifest);break;
  case 8:assert(out.status==ArfFileWrongTarget);break;
  case 9:assert(out.status==ArfFileApiOld);break;
  case 10:assert(out.status==ArfFileApiNew);break;
  case 11:assert(out.status==ArfFileTooLarge&&!preloads);break;
  case 12:assert(out.status==ArfFileNewerMinor);break;
  case 13:assert(out.status==ArfFileInUse&&!preloads);break;
  }
  assert(arf_file_status_text(out.status)[0]);
 }
 assert(!strcmp(arf_file_status_text(999),"Not checked"));return 0;
}
''')

    def test_ui_does_not_use_presence_only_ok(self):
        source = (APP / "arf_tools.c").read_text()
        self.assertNotIn('arf_tools_path_exists(app, path) ? "OK"', source)
        self.assertIn("arf_file_probe", source)
        self.assertIn("not verified", source)

    def test_managed_inventory_matches_release_contract(self):
        import re
        from tools.tumoflip.validate_release import PROTOCOL_PACKS
        path = APP / "arf_expected_packs.h"
        self.assertTrue(path.exists(), "Missing packages must also be diagnosed")
        names = set(re.findall(r'"(protocol_[a-z0-9_]+\.fal)"', path.read_text()))
        self.assertEqual(names, PROTOCOL_PACKS)


if __name__ == "__main__":
    unittest.main()
