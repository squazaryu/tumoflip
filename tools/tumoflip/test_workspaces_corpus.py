"""Execute bounded profile validation and RAW replay parsing under sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "applications/main/subghz/plugins/workspaces"
CORPUS = ROOT / "applications_user/tumo_acceptance_suite"


class WorkspacesCorpusTest(unittest.TestCase):
    def native(self, program, source, directory):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(program)
            build = subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(directory),
                str(path / "test.c"), str(directory / source), "-o", str(path / "test"),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_profile_validation(self):
        self.native(r'''
#include "workspace_model.h"
#include <assert.h>
#include <string.h>
int main(void) {
 WorkspaceProfile p={.frequency=433920000,.raw_frequency=315000000,
  .preset="AM650",.raw_preset="FM238",.pack=0,.hopping=0,.radio=0};
 assert(workspace_profile_valid(&p,8));
 assert(workspace_name_valid("Weather station 1"));
 const char* bad[]={""," ","../abc","x/y","x\\y",".hidden","a\nb","last.","a:","x ","abcdefghijklmnopqrstuvwxyz0123456789"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!workspace_name_valid(bad[i]));
 assert(!workspace_name_valid(NULL));assert(!workspace_profile_valid(NULL,8));
 p.frequency=0;assert(!workspace_profile_valid(&p,8));p.frequency=433920000;
 p.raw_frequency=UINT32_MAX;assert(!workspace_profile_valid(&p,8));p.raw_frequency=315000000;
 p.pack=8;assert(!workspace_profile_valid(&p,8));p.pack=0;
 p.hopping=4;assert(!workspace_profile_valid(&p,8));p.hopping=0;
 p.radio=3;assert(!workspace_profile_valid(&p,8));p.radio=0;
 p.preset[0]=0;assert(!workspace_profile_valid(&p,8));strcpy(p.preset,"AM650");
 memset(p.raw_preset,'x',sizeof(p.raw_preset));assert(!workspace_profile_valid(&p,8));
 return 0;
}''', "workspace_model.c", PROFILE)

    def test_streaming_raw_parser(self):
        self.native(r'''
#include "corpus_raw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int calls;
static bool emit(bool level,uint32_t duration,void*ctx){(void)ctx;assert(duration>0);if(calls==0){assert(level&&duration==350);}calls++;return true;}
static const char* header="Filetype: Flipper SubGhz RAW File\r\nVersion: 1\nFrequency: 433920000\nPreset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\n";
static bool parse(CorpusRaw*p,const char*s,size_t step){corpus_raw_init(p,emit,NULL);calls=0;for(size_t i=0;i<strlen(s);i+=step){size_t n=strlen(s)-i;if(n>step)n=step;if(!corpus_raw_feed(p,s+i,n))return false;}return corpus_raw_finish(p);}
int main(void){CorpusRaw p;char s[2048];
 snprintf(s,sizeof(s),"%sRAW_Data: 350 -700 350\nRAW_Data: -1400 350 -350",header);
 for(size_t k=1;k<80;k++){assert(parse(&p,s,k));assert(calls==6&&p.pulses==6&&p.frequency==433920000);}
 const char* bad[]={"0","-0","2147483648","-2147483648","12x","+350","-","10000001"};
 for(size_t k=0;k<sizeof(bad)/sizeof(*bad);k++){snprintf(s,sizeof(s),"%sRAW_Data: %s\n",header,bad[k]);assert(!parse(&p,s,1));}
 assert(!parse(&p,header,4));assert(!parse(&p,"RAW_Data: 350 -700\n",1));
 snprintf(s,sizeof(s),"%sFrequency: 315000000\nRAW_Data: 350 -700\n",header);assert(!parse(&p,s,7));
 snprintf(s,sizeof(s),"%sRAW_Data: 350 -700\nVersion: 1\n",header);assert(!parse(&p,s,7));
 snprintf(s,sizeof(s),"%sRAW_Data: 350 -700\n",header);char* v=strstr(s,"Version: 1");v[9]='2';assert(!parse(&p,s,7));
 corpus_raw_init(&p,emit,NULL);assert(!corpus_raw_feed(&p,"a\0b",3));
 assert(sizeof(p)<256);return 0;
}''', "corpus_raw.c", CORPUS)

    def test_corpus_is_offline_and_preserves_references(self):
        source = (CORPUS / "corpus_runner.c").read_text()
        for forbidden in ("start_async_tx", "subghz_transmitter_", "subghz_devices_", "FSOM_CREATE_ALWAYS"):
            self.assertNotIn(forbidden, source)
        for required in ("subghz_receiver_decode", "FSOM_CREATE_NEW", "storage_file_sync", "Input changed", "No decoded frames", "Cancelled"):
            self.assertIn(required, source)


if __name__ == "__main__":
    unittest.main()
