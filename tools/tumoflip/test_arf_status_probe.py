"""ARF Status must never label mere file presence as verified readiness."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/arf_tools"


class ArfStatusProbeTests(unittest.TestCase):
    def test_manifest_only_probe_and_explicit_unknown_integrity(self):
        path = APP / "arf_file_probe.c"
        self.assertTrue(path.exists(), "Metadata probe is missing")
        source = "\n".join(l for l in path.read_text().splitlines() if not l.startswith("#include"))
        header = "\n".join(l for l in (APP / "arf_file_probe.h").read_text().splitlines()
                           if not l.startswith(("#include", "#pragma")))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int Storage;
enum {FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL};
typedef int FS_Error;
typedef struct {uint64_t size;bool directory;} FileInfo;
static bool file_info_is_dir(const FileInfo* f){return f->directory;}
typedef enum {FlipperApplicationPreloadStatusSuccess,FlipperApplicationPreloadStatusInvalidFile,
 FlipperApplicationPreloadStatusNotEnoughMemory,FlipperApplicationPreloadStatusInvalidManifest,
 FlipperApplicationPreloadStatusApiTooOld,FlipperApplicationPreloadStatusApiTooNew,
 FlipperApplicationPreloadStatusTargetMismatch} FlipperApplicationPreloadStatus;
typedef struct {struct {struct {uint16_t major,minor;} api_version;uint16_t hardware_target_id;} base;
 uint32_t app_version;char name[32];} FlipperApplicationManifest;
typedef int FlipperApplication;
static int api,allocated,preloads,mode;static const int* firmware_api_interface=&api;
static FlipperApplicationManifest manifest;
static FS_Error storage_common_stat(Storage* s,const char* p,FileInfo* out){
 (void)s;(void)p;if(mode==1)return FSE_NOT_EXIST;if(mode==2)return FSE_INTERNAL;
 out->directory=mode==3;out->size=mode==4?0:1024;return FSE_OK;
}
static FlipperApplication* flipper_application_alloc(Storage* s,const void* a){
 (void)s;assert(a==firmware_api_interface);allocated++;return &api;
}
static void flipper_application_free(FlipperApplication* a){(void)a;allocated--;}
static FlipperApplicationPreloadStatus flipper_application_preload_manifest(FlipperApplication* a,const char* p){
 (void)a;(void)p;preloads++;return mode>=5?(FlipperApplicationPreloadStatus)(mode-4):FlipperApplicationPreloadStatusSuccess;
}
static const FlipperApplicationManifest* flipper_application_get_manifest(FlipperApplication* a){(void)a;return &manifest;}
''' + header + source + r'''
int main(void){
 manifest.base.api_version.major=88;manifest.base.api_version.minor=9;
 manifest.base.hardware_target_id=7;manifest.app_version=3;memset(manifest.name,'x',32);
 for(mode=0;mode<=10;mode++){
  preloads=allocated=0;ArfFileProbe out;memset(&out,0x77,sizeof(out));
  arf_file_probe(NULL,"test.fap",&out);
  assert(!allocated && !out.integrity_verified && !out.imports_verified);
  if(mode==0){assert(out.status==ArfFileHeaderCompatible && out.api_major==88 && out.api_minor==9);assert(out.name[32]==0);}
  else if(mode==1)assert(out.status==ArfFileMissing && !preloads);
  else if(mode==2)assert(out.status==ArfFileIoError && !preloads);
  else if(mode==3)assert(out.status==ArfFileNotRegular && !preloads);
  else if(mode==4)assert(out.status==ArfFileEmpty && !preloads);
  else assert(out.status!=ArfFileHeaderCompatible && preloads==1);
  assert(arf_file_status_text(out.status)[0]);
 }
 return 0;
}
''')

    def test_ui_does_not_use_presence_only_ok(self):
        source = (APP / "arf_tools.c").read_text()
        self.assertNotIn('arf_tools_path_exists(app, path) ? "OK"', source)
        self.assertIn("arf_file_probe", source)
        self.assertIn("not verified", source)


if __name__ == "__main__":
    unittest.main()
