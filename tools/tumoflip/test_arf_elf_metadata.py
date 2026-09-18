"""Bounded metadata parsing must reject malformed ELF without invoking its loader."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/arf_tools"


class ArfElfMetadataTests(unittest.TestCase):
    def test_native_bounds_and_real_fap(self):
        fixture = r'''
#include "arf_elf_metadata.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t data[512],baseline[512];static bool io_fail;
static void put16(size_t at,uint16_t v){data[at]=v;data[at+1]=v>>8;}
static void put32(size_t at,uint32_t v){put16(at,v);put16(at+2,v>>16);}
static bool read_at(void* ctx,uint32_t at,void* out,size_t n){(void)ctx;assert(n<=85);if(io_fail||at>sizeof(data)||n>sizeof(data)-at)return false;memcpy(out,data+at,n);return true;}
static bool file_read(void* ctx,uint32_t at,void* out,size_t n){return !fseek(ctx,at,SEEK_SET)&&fread(out,1,n,ctx)==n;}
int main(int argc,char** argv){
 memcpy(data,"\177ELF\1\1\1",7);put16(16,1);put16(18,40);put32(20,1);
 put32(32,64);put16(40,52);put16(46,40);put16(48,3);put16(50,1);
 put32(104+4,3);put32(104+16,256);put32(104+20,32);
 put32(144,1);put32(144+4,1);put32(144+16,320);put32(144+20,85);
 memcpy(data+256,"\0.fapmeta\0",10);
 put32(320,0x52474448);put32(324,1);put16(328,9);put16(330,88);put16(332,7);
 put32(336,0x00010002);memcpy(data+340,"Example",8);
 memcpy(baseline,data,sizeof(data));ArfElfMetadata out;
 assert(arf_elf_metadata_read(read_at,NULL,sizeof(data),&out)==ArfElfOk);
 assert(out.api_major==88&&out.api_minor==9&&out.target==7&&out.app_version==0x10002&&!strcmp(out.name,"Example"));
 for(int mode=0;mode<12;mode++){
  memcpy(data,baseline,sizeof(data));
  switch(mode){
  case 0:put32(32,0xfffffff0);break;
  case 1:put16(48,65535);break;
  case 2:put16(50,4);break;
  case 3:data[5]=2;break;
  case 4:put16(18,3);break;
  case 5:data[258]='x';break;
  case 6:put32(144+20,86);break;
  case 7:put32(144+16,500);break;
  case 8:put32(144,33);break;
  case 9:memset(data+256,'x',32);break;
  case 10:put32(320,0);break;
  case 11:put32(324,2);break;
  }
  assert(arf_elf_metadata_read(read_at,NULL,sizeof(data),&out)!=ArfElfOk);
 }
 memcpy(data,baseline,sizeof(data));io_fail=true;
 assert(arf_elf_metadata_read(read_at,NULL,sizeof(data),&out)==ArfElfIoError);
 io_fail=false;assert(arf_elf_metadata_read(read_at,NULL,24,&out)==ArfElfInvalid);
 put16(48,4);put32(184+4,8);put32(184+16,0xffffffff);put32(184+20,65536);
 assert(arf_elf_metadata_read(read_at,NULL,sizeof(data),&out)==ArfElfOk);
 memcpy(data,baseline,sizeof(data));put32(64,1);put32(68,1);put32(80,320);put32(84,85);
 assert(arf_elf_metadata_read(read_at,NULL,sizeof(data),&out)==ArfElfInvalid);
 if(argc>1){FILE* f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);
  assert(arf_elf_metadata_read(file_read,f,n,&out)==ArfElfOk);assert(out.target==7&&out.api_major==88);fclose(f);}
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "test.c").write_text(fixture)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(APP), str(work / "test.c"),
                str(APP / "arf_elf_metadata.c"), "-o", str(work / "test")], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            command = [str(work / "test")]
            fap = ROOT / "build/f7-firmware-C/.extapps/capture_inspector.fap"
            if fap.exists(): command.append(str(fap))
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
