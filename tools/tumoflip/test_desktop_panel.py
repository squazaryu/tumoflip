"""Independent desktop profiles and bounded motion, actual serializer on fake storage."""
from pathlib import Path
import os
import json
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tools/tumoflip/fixtures/desktop_settings_strings"
COVERAGE = "--coverage" in sys.argv
if COVERAGE:
    sys.argv.remove("--coverage")

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "applications/services/desktop/desktop_panel_io_i.h"

struct Storage {unsigned unused;};
struct File {int bank;size_t position;};
static struct {uint8_t bytes[1024];size_t size;bool exists;} files[2];
static Storage storage;
static int write_budget=-1;
static unsigned writes;
static bool fail_reads;
void* furi_record_open(const char* name){assert(!strcmp(name,RECORD_STORAGE));return &storage;}
void furi_record_close(const char* name){assert(!strcmp(name,RECORD_STORAGE));}
File* storage_file_alloc(Storage*s){assert(s==&storage);File*f=calloc(1,sizeof(*f));f->bank=-1;return f;}
bool storage_file_open(File*f,const char* path,FS_AccessMode access,FS_OpenMode mode){
 assert(f&&path);(void)mode;
 if(!strcmp(path,DESKTOP_PANEL_BANK0))f->bank=0;
 else if(!strcmp(path,DESKTOP_PANEL_BANK1))f->bank=1;
 else assert(!"Unexpected path: the wallpaper settings must not be read/written by this module");
 f->position=0;
 if(access==FSAM_READ)return !fail_reads&&files[f->bank].exists;
 assert(access==FSAM_WRITE);files[f->bank].size=0;files[f->bank].exists=true;writes++;return true;
}
size_t storage_file_read(File*f,void*data,size_t size){
 assert(f->bank>=0);size_t available=files[f->bank].size-f->position;
 if(size>available){
  size=available;
 }
 memcpy(data,files[f->bank].bytes+f->position,size);
 f->position+=size;
 return size;
}
size_t storage_file_write(File*f,const void*data,size_t size){
 assert(f->bank>=0);if(write_budget>=0&&(int)size>write_budget)size=write_budget;
 assert(f->position+size<=sizeof(files[0].bytes));memcpy(files[f->bank].bytes+f->position,data,size);
 f->position+=size;files[f->bank].size=f->position;if(write_budget>=0)write_budget-=size;return size;
}
uint64_t storage_file_size(File*f){return files[f->bank].size;}
const char* storage_file_get_error_desc(File*f){(void)f;return "synthetic";}
void storage_file_close(File*f){(void)f;}
void storage_file_free(File*f){free(f);}
/* Same reflected CRC32 contract as the production exported helper. */
uint32_t crc32_calc_buffer(uint32_t crc,const void*bytes,size_t length){
 const uint8_t*p=bytes;crc=~crc;while(length--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);}return ~crc;
}
static DesktopSettings wallpaper(void){
 DesktopSettings settings={.auto_lock_delay_ms=60000,.display_clock=1};
 for(unsigned i=0;i<FavoriteAppNumber;i++)snprintf(settings.favorite_apps[i].name_or_path,128,"Original %u",i);
 return settings;
}
static void defaults_case(void){
 _Static_assert(sizeof(DesktopSettings)==648,"Existing public settings ABI must not change");
 DesktopSettings old=wallpaper(), before=old;DesktopPanelSettings panel;
 desktop_panel_defaults(&panel,&old);
 assert(panel.mode==DesktopModeWallpaper&&panel.motion==1&&panel.generation==0&&desktop_panel_valid(&panel));
 panel.motion=2;assert(!desktop_panel_valid(&panel));panel.motion=0;assert(desktop_panel_valid(&panel));panel.motion=1;
 assert(!memcmp(&old,&before,sizeof(old))&&!writes);
 assert(!strcmp(desktop_panel_held(&panel,FavoriteAppLeftLong)->name_or_path,"Original 1"));
 assert(!strcmp(desktop_panel_held(&panel,FavoriteAppRightLong)->name_or_path,"Original 3"));
 assert(!strcmp(desktop_panel_held(&panel,FavoriteAppOkLong)->name_or_path,"Original 4"));
 assert(!desktop_panel_held(&panel,FavoriteAppLeftShort));
 assert(!desktop_panel_held(&panel,FavoriteAppRightShort)&&!desktop_panel_held(&panel,99));
 strcpy(desktop_panel_held(&panel,FavoriteAppLeftLong)->name_or_path,"Panel app");
 assert(!memcmp(&old,&before,sizeof(old)));
 panel.mode=99;assert(!desktop_panel_valid(&panel));panel.mode=DesktopModeDashboard;
 memset(panel.held[0].name_or_path,'A',128);assert(!desktop_panel_valid(&panel));
 desktop_panel_defaults(&panel,&old);panel.reserved[0]=1;assert(!desktop_panel_valid(&panel));
}
static void banks_case(void){
 DesktopSettings old=wallpaper(), before=old;DesktopPanelSettings panel, read;
 desktop_panel_defaults(&panel,&old);read=panel;assert(!desktop_panel_load(&read)&&!memcmp(&read,&panel,sizeof(read))&&!writes);
 panel.mode=DesktopModeDashboard;assert(desktop_panel_store(&panel)&&panel.generation==1);
 assert(desktop_panel_load(&read)&&!memcmp(&panel,&read,sizeof(read)));
 DesktopPanelSettings stale=panel;
 strcpy(panel.held[1].name_or_path,"Panel right");assert(desktop_panel_store(&panel)&&panel.generation==2);
 unsigned count=writes;assert(!desktop_panel_store(&stale)&&writes==count); // No stale overwrite.
 files[1].bytes[20]^=1;assert(desktop_panel_load(&read)&&read.generation==1);
 assert(!memcmp(&old,&before,sizeof(old)));
 fail_reads=true;read=panel;assert(!desktop_panel_load(&read)&&!memcmp(&read,&panel,sizeof(read)));
 assert(!desktop_panel_store(&panel));fail_reads=false;
}
static void cuts_case(void){
 for(unsigned cut=0;cut<=sizeof(DesktopPanelRecord)+8;cut++){
  memset(files,0,sizeof(files));write_budget=-1;DesktopSettings old=wallpaper();DesktopPanelSettings panel,read;
  desktop_panel_defaults(&panel,&old);assert(desktop_panel_store(&panel));
  panel.mode=DesktopModeDashboard;strcpy(panel.held[0].name_or_path,"New choice");
  write_budget=cut;bool saved=desktop_panel_store(&panel);write_budget=-1;
  assert(desktop_panel_load(&read));
  assert(read.generation==1||read.generation==2);
  if(read.generation==1)assert(read.mode==DesktopModeWallpaper&&!strcmp(read.held[0].name_or_path,"Original 1"));
  else assert(read.mode==DesktopModeDashboard&&!strcmp(read.held[0].name_or_path,"New choice"));
  assert(saved==(cut>=sizeof(DesktopPanelRecord)+8));
 }
}
static void motion_case(void){
 assert(desktop_panel_page(0,-1)==2&&desktop_panel_page(2,1)==0);
 assert(desktop_panel_page(1,0)==1&&desktop_panel_page(99,1)==0);
 assert(desktop_panel_offset(0,0,1,true)==112);
 assert(desktop_panel_offset(0,80,1,true)==56);
 assert(desktop_panel_offset(0,159,-1,true)==0);
 assert(!desktop_panel_offset(0,160,1,true)&&!desktop_panel_offset(0,1,1,false));
 assert(desktop_panel_offset(UINT32_MAX-50,29,-1,true)==-56);
 assert(!desktop_panel_offset(0,1,0,true));
 assert(desktop_panel_period(false,true,1,true,1)==0);
 assert(desktop_panel_period(true,true,2,false,0)==40);
 assert(desktop_panel_period(true,false,1,false,0)==250);
 assert(desktop_panel_period(true,false,0,true,0)==250);
 assert(desktop_panel_period(true,false,0,false,1)==250);
 assert(!desktop_panel_period(true,false,2,true,0));
 for(unsigned i=0;i<10000;i++)assert(desktop_panel_icon_frame(i,8)<8);
 assert(!desktop_panel_icon_frame(100,0));
}
static void edges_case(void){
 DesktopSettings old=wallpaper();DesktopPanelSettings panel,read;
 desktop_panel_defaults(&panel,&old);assert(!desktop_panel_valid(NULL)&&!desktop_panel_held(NULL,99));
 for(unsigned i=0;i<2;i++){
  panel.reserved[i]=1;assert(!desktop_panel_valid(&panel));panel.reserved[i]=0;
  memset(panel.held[i].name_or_path,'Z',128);assert(!desktop_panel_valid(&panel));desktop_panel_defaults(&panel,&old);
 }
 panel.mode=99;assert(!desktop_panel_store(&panel)&&!writes);panel.mode=0;
 assert(desktop_panel_store(&panel));uint8_t baseline[sizeof(files)];memcpy(baseline,files,sizeof(files));
 for(unsigned header=0;header<2;header++){
  files[0].bytes[header]^=1;assert(!desktop_panel_load(&read));memcpy(files,baseline,sizeof(files));
 }
 files[0].size++;assert(!desktop_panel_load(&read));memcpy(files,baseline,sizeof(files));
 // Compensating changes pass SavedStruct's additive checksum, but not our CRC32.
 files[0].bytes[16]++;files[0].bytes[17]--;assert(!desktop_panel_load(&read));memcpy(files,baseline,sizeof(files));
 DesktopPanelRecord record={.settings=panel};record.settings.generation=UINT32_MAX;
 record.crc=crc32_calc_buffer(0,&record.settings,sizeof(record.settings));
 assert(saved_struct_save(DESKTOP_PANEL_BANK0,&record,sizeof(record),DESKTOP_PANEL_MAGIC,DESKTOP_PANEL_VERSION));
 assert(desktop_panel_load(&panel)&&panel.generation==UINT32_MAX);
 panel.mode=DesktopModeDashboard;assert(desktop_panel_store(&panel)&&panel.generation==1);
 assert(desktop_panel_load(&read)&&read.generation==1&&read.mode==DesktopModeDashboard);
}
static void confirm_case(void){
 DesktopSettings old=wallpaper();DesktopPanelSettings panel,stale;
 desktop_panel_defaults(&panel,&old);assert(desktop_panel_store(&panel));
 unsigned count=writes;assert(desktop_panel_store(&panel)&&writes==count&&panel.generation==1);
 panel.mode=DesktopModeDashboard;stale=panel;assert(desktop_panel_store(&panel)&&panel.generation==2);
 count=writes;assert(desktop_panel_store(&stale)&&stale.generation==2&&writes==count);
}
int main(int argc,char**argv){assert(argc==2);
 if(!strcmp(argv[1],"defaults"))defaults_case();else if(!strcmp(argv[1],"banks"))banks_case();
 else if(!strcmp(argv[1],"cuts"))cuts_case();else if(!strcmp(argv[1],"edges"))edges_case();
 else if(!strcmp(argv[1],"confirm"))confirm_case();else motion_case();
 return 0;
}
'''


class DesktopPanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="desktop-panel-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        (work / "case.c").write_text(HARNESS)
        cls.binary = work / "test"
        cls.work = work
        cls.environment = dict(os.environ, LLVM_PROFILE_FILE=str(work/"run-%p.profraw"))
        compiled = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            *(["-fprofile-instr-generate", "-fcoverage-mapping"] if COVERAGE else []),
            "-I", str(FIXTURES), "-I", str(ROOT), "-I", str(ROOT / "lib/toolbox"),
            str(work / "case.c"), str(ROOT / "lib/toolbox/saved_struct.c"), "-o", str(cls.binary),
        ], capture_output=True, text=True, timeout=30)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, timeout=20, env=self.environment)

    def test_fixture_compiles_with_strict_gcc_diagnostics(self):
        candidates = [ROOT/"toolchain/current/bin",ROOT/"toolchain/x86_64-linux/bin",ROOT/"toolchain/arm64-darwin/bin"]
        compiler=next((path/"arm-none-eabi-gcc" for path in candidates if (path/"arm-none-eabi-gcc").exists()),None)
        self.assertIsNotNone(compiler,"The bundled ARM GCC is required for fixture portability")
        result=subprocess.run([str(compiler),"-std=c11","-Wall","-Wextra","-Werror","-fsyntax-only",
            "-I",str(FIXTURES),"-I",str(ROOT),"-I",str(ROOT/"lib/toolbox"),str(self.work/"case.c")],
            capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)

    @classmethod
    def tearDownClass(cls):
        if not COVERAGE:
            return
        from tools.tumoflip.test_desktop_settings_strings import llvm_tool
        profile=cls.work/"coverage.profdata"
        subprocess.run([llvm_tool("llvm-profdata"),"merge","-sparse",*map(str,cls.work.glob("*.profraw")),"-o",str(profile)],check=True,capture_output=True)
        coverage=json.loads(subprocess.check_output([llvm_tool("llvm-cov"),"export",str(cls.binary),f"-instr-profile={profile}"],text=True))
        for name in ("desktop_panel_i.h","desktop_panel_io_i.h"):
            summary=next(entry["summary"] for entry in coverage["data"][0]["files"] if entry["filename"].endswith("/"+name))
            for metric in ("lines","regions"):
                result=summary[metric]
                print(f"{name} {metric}: {result['covered']}/{result['count']} ({result['percent']:.2f}%)")
                if result["percent"]<80:raise AssertionError(f"{name}: {metric} coverage below 80%")

    def test_wallpaper_abi_defaults_and_profile_independence(self):
        self.run_case("defaults")

    def test_validated_banks_corruption_and_stale_write_rejection(self):
        self.run_case("banks")

    def test_every_partial_write_preserves_a_complete_profile(self):
        self.run_case("cuts")

    def test_motion_wraparound_retarget_limits_and_sleep(self):
        self.run_case("motion")

    def test_crc_schema_validation_and_generation_wraparound(self):
        self.run_case("edges")

    def test_retry_confirms_identical_data_without_another_write(self):
        self.run_case("confirm")


if __name__ == "__main__":
    unittest.main()
