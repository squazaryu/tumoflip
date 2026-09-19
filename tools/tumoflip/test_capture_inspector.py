"""Native tests of the bounded, radio-free capture model."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/capture_inspector"


class CaptureInspectorTests(unittest.TestCase):
    def test_native_parser_and_field_comparison(self):
        program = r'''
#include "capture_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* valid =
 "Filetype: Flipper SubGhz Key File\r\nVersion: 1\r\n"
 "Frequency: 433920000\nPreset: AM650\nProtocol: Example\n"
 "Bit: 24\nKey: 00 00 00 00 00 12 34 56\nUnknown: alpha\n";
static CiParseStatus parse(CiSnapshot* s,const char* text,size_t chunk) {
 ci_snapshot_reset(s);
 for(size_t n=0;n<strlen(text);n+=chunk) {
  size_t k=strlen(text)-n;if(k>chunk)k=chunk;
  ci_snapshot_feed(s,text+n,k);
 }
 return ci_snapshot_finish(s);
}
int main(void) {
 CiSnapshot *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));
 assert(a && b && sizeof(*a)<=6000);
 for(size_t chunk=1;chunk<40;chunk++) {
  assert(parse(a,valid,chunk)==CiParseOk);
  assert(a->count==8);
  assert(!strcmp(ci_snapshot_find(a,"Unknown",0)->value,"alpha"));
 }
 assert(parse(b,valid,7)==CiParseOk);
 assert(ci_diff_count(a,b)==8);
 for(size_t i=0;i<8;i++){CiDiffRow r;assert(ci_diff_row(a,b,i,&r));assert(r.kind==CiDiffSame);}
 CiDiffRow r;assert(!ci_diff_row(a,b,8,&r));
 char input[8192];
 snprintf(input,sizeof(input),"%sUnknown: beta\nExtra: one\n",valid);
 assert(parse(b,input,1)==CiParseOk && b->count==10);
 assert(ci_diff_count(a,b)==10);
 assert(ci_diff_row(a,b,8,&r)&&r.kind==CiDiffOnlyB&&!strcmp(r.key,"Unknown")&&r.occurrence==1);
 assert(ci_diff_row(b,a,8,&r)&&r.kind==CiDiffOnlyA);
 snprintf(input,sizeof(input),"# comment\nVersion: 1\n Filetype : Flipper SubGhz Key File\n"
 "Key: 00 00 00 00 00 12 34 56\nBit: 24\nUnknown: alpha\nProtocol: Example\n"
 "Preset: AM650\nFrequency: 433920000");
 assert(parse(b,input,3)==CiParseOk);
 for(size_t i=0;i<ci_diff_count(a,b);i++){assert(ci_diff_row(a,b,i,&r));assert(r.kind==CiDiffSame);}
 snprintf(input,sizeof(input),"%sVersion: 1\n",valid);
 assert(parse(b,input,3)==CiParseDuplicateHeader);
 assert(ci_diff_count(a,b)==0);
 assert(parse(b,"Filetype: Flipper SubGhz RAW File\nVersion: 1\n",2)==CiParseUnsupported);
 assert(parse(b,"Filetype: Other\nVersion: 1\n",1)==CiParseUnsupported);
 assert(parse(b,"Filetype: Flipper SubGhz Key File\nVersion: 2\n",1)==CiParseUnsupported);
 assert(parse(b,"Filetype: Flipper SubGhz Key File\nVersion: 1\n",1)==CiParseMissingField);
 assert(parse(b,"bad line",1)==CiParseMalformed);
 strcpy(input,valid);strcat(input,"More: ");size_t n=strlen(input);
 memset(input+n,'x',CI_VALUE_CAP);input[n+CI_VALUE_CAP]=0;
 assert(parse(b,input,1)==CiParseLimit);
 strcpy(input,valid);for(unsigned i=0;i<CI_FIELD_CAP;i++)strcat(input,"Extra: yes\n");
 assert(parse(b,input,13)==CiParseLimit);
 ci_snapshot_reset(b);ci_snapshot_feed(b,"x\0y",3);assert(ci_snapshot_finish(b)==CiParseMalformed);
 snprintf(input,sizeof(input),"%s",valid);char* freq=strstr(input,"433920000");
 memcpy(freq,"000000000",9);assert(parse(b,input,2)==CiParseMissingField);
 snprintf(input,sizeof(input),"%sCRC: stored-value\n",valid);
 assert(parse(b,input,17)==CiParseOk);
 assert(!strcmp(ci_snapshot_find(b,"CRC",0)->value,"stored-value"));
 snprintf(input,sizeof(input),"%s",valid);char* value=strstr(input,"alpha");memcpy(value,"omega",5);
 assert(parse(b,input,5)==CiParseOk);
 assert(ci_diff_row(a,b,7,&r)&&r.kind==CiDiffChanged);
 ci_snapshot_reset(b);char comment[256];memset(comment,' ',sizeof(comment));comment[0]='#';comment[255]='\n';
 for(unsigned i=0;i<(CI_FILE_CAP/256)+1;i++)ci_snapshot_feed(b,comment,sizeof(comment));
 assert(ci_snapshot_finish(b)==CiParseLimit);
 assert(strstr(ci_parse_status_text(CiParseLimit),"limit"));
 assert(!ci_snapshot_find(a,"Absent",0));
 for(unsigned i=0;i<8;i++)assert(ci_parse_status_text((CiParseStatus)i));
 assert(ci_snapshot_finish(a)==CiParseOk);assert(ci_snapshot_feed(a,"ignored",7)==CiParseOk);
 ci_snapshot_reset(b);assert(ci_snapshot_feed(b,NULL,1)==CiParseMalformed);
 ci_snapshot_reset(b);assert(!ci_diff_row(a,b,0,&r));
 assert(parse(b,"Empty: \n",1)==CiParseMalformed);
 assert(parse(b," : value\n",1)==CiParseMalformed);
 memset(input,'k',CI_KEY_CAP);strcpy(input+CI_KEY_CAP,": value\n");
 assert(parse(b,input,1)==CiParseLimit);
 memset(input,'k',CI_LINE_CAP+4);input[CI_LINE_CAP+4]=0;
 assert(parse(b,input,1)==CiParseLimit);
 const char* bad_freqs[]={"4294967296","12x","0"};
 for(unsigned i=0;i<3;i++){
  snprintf(input,sizeof(input),"Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: %s\nPreset: AM650\nProtocol: Example\nKey: 01\n",bad_freqs[i]);
  assert(parse(b,input,1)==CiParseMissingField);
 }
 assert(parse(b,"Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\nPreset: AM650\nProtocol: Example\n",1)==CiParseMissingField);
 assert(parse(b,"Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\nPreset: AM650\n",1)==CiParseMissingField);
 assert(parse(b,"Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n",1)==CiParseMissingField);
 printf("snapshot_bytes=%zu\n",sizeof(*a));
 free(a);free(b);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(program)
            result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                 "-I", str(APP), str(path / "test.c"), str(APP / "capture_model.c"),
                 "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_entry_points_and_package_only_boundary(self):
        fam = APP / "application.fam"
        self.assertTrue(fam.exists(), "Inspector package is missing")
        source = fam.read_text()
        self.assertIn("fap_package_only=True", source)
        self.assertIn('appid="capture_inspector"', source)
        hub = (ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c").read_text()
        saved = (ROOT / "applications/main/subghz/scenes/subghz_scene_saved_menu.c").read_text()
        self.assertIn("capture_inspector.fap", hub)
        self.assertIn("capture_inspector.fap", saved)
        from tools.tumoflip.validate_release import PACKAGE_ONLY_PACKAGE_FILES, PACKAGE_ONLY_PACKAGE_GROUPS
        target = "apps_data/arf_subghz_full/packages/capture_inspector.fap"
        self.assertIn(target, PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(PACKAGE_ONLY_PACKAGE_GROUPS[target], "arf")
        self.assertIn('loader_enqueue_launch(loader, "Sub-GHz", NULL', saved)

    def test_no_radio_or_source_write_path(self):
        storage = (APP / "capture_storage.c").read_text()
        self.assertNotIn("FSAM_WRITE", storage)
        self.assertNotIn("FSAM_READ_WRITE", storage)
        for file in APP.glob("*.c"):
            source = file.read_text()
            for marker in ("subghz_tx_start", "start_async_tx", "subghz_transmitter_", "_seed_recover"):
                self.assertNotIn(marker, source)


if __name__ == "__main__":
    unittest.main()
