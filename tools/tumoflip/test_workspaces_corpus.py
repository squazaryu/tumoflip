"""Execute bounded profile validation and RAW replay parsing under sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

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
static bool reject_emit;
static bool emit(bool level,uint32_t duration,void*ctx){(void)ctx;assert(duration>0);if(calls==0){assert(level&&duration==350);}calls++;return !reject_emit;}
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
 assert(!corpus_raw_feed(&p,"x",1));
 corpus_raw_init(&p,emit,NULL);assert(!corpus_raw_feed(&p,NULL,1));
 corpus_raw_init(&p,emit,NULL);p.bytes=CORPUS_RAW_MAX_BYTES;assert(!corpus_raw_feed(&p,"x",1));
 snprintf(s,sizeof(s),"# comment\n%sRAW_Data:\t350 -700\r\n",header);assert(parse(&p,s,1));
 reject_emit=true;assert(!parse(&p,s,1));reject_emit=false;
 snprintf(s,sizeof(s),"%sRAW_Data: 350 -700\n",header);
 corpus_raw_init(&p,emit,NULL);p.pulses=CORPUS_RAW_MAX_PULSES;p.headers=31;p.raw=true;
 assert(!corpus_raw_feed(&p,"350 ",4));
 const char* invalid[]={"Filetype: Other\n","Version: 9\n","Frequency: 0\n", "Frequency: 4294967296\n","Frequency: 43x\n","Preset: \n","Protocol: Nice\n"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!parse(&p,invalid[i],2));
 memset(s,'x',40);s[40]=0;assert(!parse(&p,s,2));
 strcpy(s,"Unknown: ");memset(s+9,'x',600);s[609]=0;assert(!parse(&p,s,1));
 strcpy(s,"Preset: ");memset(s+8,'x',70);s[78]='\n';s[79]=0;assert(!parse(&p,s,1));
 snprintf(s,sizeof(s),"%sUnknown: preserved\nRAW_Data: 350 -700\n",header);assert(parse(&p,s,1));
 snprintf(s,sizeof(s),"%sCustom_preset_module: CC1101\nCustom_preset_data: ",header);
 for(unsigned i=0;i<100;i++)strcat(s,"00 ");strcat(s,"\nRAW_Data: 350 -700\n");
 assert(parse(&p,s,1));
 assert(sizeof(p)<768);return 0;
}''', "corpus_raw.c", CORPUS)

    def test_corpus_is_offline_and_preserves_references(self):
        source = (CORPUS / "corpus_runner.c").read_text()
        for forbidden in ("start_async_tx", "subghz_transmitter_", "subghz_devices_", "FSOM_CREATE_ALWAYS"):
            self.assertNotIn(forbidden, source)
        for required in ("subghz_receiver_decode", "FSOM_CREATE_NEW", "storage_file_sync", "Input changed", "No decoded frames", "Cancelled"):
            self.assertIn(required, source)

    def test_profile_apply_and_rollback_execute_real_handler(self):
        source = (PROFILE / "workspaces.c").read_text()
        body = function(source, "static void workspace_apply(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int SubGhzRadioDeviceType;
typedef int SubGhzProtocolPackGroup;
typedef int SubGhzSetting;
typedef struct {int radio,pack;} Radio;
typedef struct {unsigned loaded_plugin_count,expected_plugin_count;} SubGhzProtocolPackReport;
typedef struct {uint32_t frequency,raw_frequency,preset_index,raw_preset_index,protocol_pack_group,hopping_mode;} Settings;
typedef struct {Radio*txrx;Settings*last_settings;} SubGhz;
typedef struct {uint32_t frequency,raw_frequency,pack,hopping,radio;char preset[32],raw_preset[32];} Profile;
typedef struct {SubGhz*app;Profile profile;} Workspace;
static bool missing_radio,bad_frequency;static int bad_pack=-1;
static char message[200];static int setting;
static SubGhzSetting* subghz_txrx_get_setting(Radio*r){(void)r;return &setting;}
static int subghz_setting_get_inx_preset_by_name(int*s,const char*n){(void)s;return !strcmp(n,"AM650")?1:!strcmp(n,"FM238")?2:-1;}
static int subghz_txrx_radio_device_get(Radio*r){return r->radio;}
static int subghz_txrx_get_protocol_pack_group(Radio*r){return r->pack;}
static int subghz_txrx_radio_device_set(Radio*r,int n){r->radio=missing_radio&&n==2?1:n;return r->radio;}
static bool subghz_txrx_radio_device_is_frequency_valid(Radio*r,uint32_t n){(void)r;(void)n;return !bad_frequency;}
static bool subghz_txrx_reload_protocol_pack(Radio*r,int n){r->pack=n;return true;}
static const SubGhzProtocolPackReport* subghz_txrx_get_protocol_pack_report(Radio*r){static SubGhzProtocolPackReport p;p.expected_plugin_count=1;p.loaded_plugin_count=r->pack==bad_pack?0:1;return &p;}
static void workspace_message(Workspace*w,const char*t){(void)w;strcpy(message,t);}
''' + body + r'''
int main(void){
 Settings before={433920000,315000000,1,2,0,0},s=before;Radio radio={1,0};SubGhz app={&radio,&s};
 Workspace w={&app,{868350000,433920000,2,3,2,"FM238","AM650"}};
 missing_radio=true;workspace_apply(&w);assert(!memcmp(&s,&before,sizeof(s))&&radio.radio==1&&radio.pack==0);assert(strstr(message,"Radio unavailable"));
 missing_radio=false;bad_pack=2;workspace_apply(&w);assert(!memcmp(&s,&before,sizeof(s))&&radio.radio==1&&radio.pack==0);
 bad_pack=-1;bad_frequency=true;workspace_apply(&w);assert(!memcmp(&s,&before,sizeof(s))&&radio.radio==1);
 bad_frequency=false;strcpy(w.profile.preset,"missing");workspace_apply(&w);assert(!memcmp(&s,&before,sizeof(s))&&strstr(message,"Preset unavailable"));
 strcpy(w.profile.preset,"FM238");workspace_apply(&w);
 assert(s.frequency==868350000&&s.raw_frequency==433920000&&s.preset_index==2&&s.raw_preset_index==1);
 assert(s.protocol_pack_group==2&&s.hopping_mode==3&&radio.pack==2&&radio.radio==2);
 assert(strstr(message,"this session"));return 0;}
''')

    def test_corpus_completion_mismatch_errors_and_cancellation(self):
        source = (CORPUS / "corpus_runner.c").read_text()
        body = function(source, "bool corpus_runner_step(") + function(source, "void corpus_runner_cancel(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
enum{FSE_OK=0};
typedef struct{uint32_t frames;unsigned char input[32],output[32];}Ref;
typedef struct{uint32_t bytes,frequency;}Parser;
typedef struct{bool running,batch,candidate;void*input;uint32_t started,size,passed,failed,slot;int input_hash,output_hash;Parser parser;Ref actual,expected;char text[500],details[40];}CorpusRunner;
static int read_error,read_size,now,closed,parse_error;
static size_t storage_file_read(void*f,void*b,size_t n){(void)f;(void)n;memset(b,'x',read_size);return read_size;}
static int storage_file_get_error(void*f){(void)f;return read_error;}
static unsigned furi_get_tick(void){return now;}
static unsigned furi_ms_to_ticks(unsigned n){return n;}
static int mbedtls_sha256_update(void*c,const void*b,size_t n){(void)c;(void)b;(void)n;return 0;}
static bool corpus_raw_feed(Parser*p,const char*b,size_t n){(void)b;p->bytes+=n;return !parse_error;}
static bool corpus_raw_finish(Parser*p){(void)p;return !parse_error;}
static int mbedtls_sha256_finish(int*c,unsigned char*d){memset(d,*c==1?'a':'b',32);return 0;}
static void corpus_close(CorpusRunner*r){(void)r;closed++;}
static bool corpus_next(CorpusRunner*r){r->running=false;return false;}
static void furi_string_set(char*s,const char*v){strcpy(s,v);}
static void furi_string_cat_printf(char*s,const char*f,...){va_list a;va_start(a,f);vsnprintf(s+strlen(s),500-strlen(s),f,a);va_end(a);}
static void furi_string_printf(char*s,const char*f,...){va_list a;va_start(a,f);vsnprintf(s,500,f,a);va_end(a);}
static const char*furi_string_get_cstr(const char*s){return s;}
''' + body + r'''
static CorpusRunner setup(bool batch){CorpusRunner r={0};r.running=true;r.batch=batch;r.input_hash=1;r.output_hash=2;r.slot=1;r.size=r.parser.bytes=40;r.actual.frames=r.expected.frames=2;memset(r.expected.input,'a',32);memset(r.expected.output,'b',32);read_error=read_size=now=parse_error=closed=0;return r;}
int main(void){CorpusRunner r=setup(true);assert(!corpus_runner_step(&r)&&r.passed==1&&closed==1);
 r=setup(true);r.expected.input[0]=0;assert(!corpus_runner_step(&r)&&r.failed==1&&strstr(r.text,"Input changed"));
 r=setup(true);r.expected.output[0]=0;assert(!corpus_runner_step(&r)&&r.failed==1&&strstr(r.text,"Decoder result changed"));
 r=setup(true);r.expected.frames++;assert(!corpus_runner_step(&r)&&r.failed==1);
 r=setup(false);assert(!corpus_runner_step(&r)&&r.candidate);
 r=setup(false);r.actual.frames=0;assert(!corpus_runner_step(&r)&&!r.candidate&&strstr(r.text,"No decoded frames"));
 r=setup(false);r.parser.bytes--;assert(!corpus_runner_step(&r)&&!r.candidate&&strstr(r.text,"Truncated"));
 r=setup(false);read_error=1;assert(!corpus_runner_step(&r)&&strstr(r.text,"read error"));
 r=setup(false);now=120001;assert(!corpus_runner_step(&r)&&strstr(r.text,"timeout"));
 r=setup(false);read_size=10;assert(corpus_runner_step(&r)&&closed==0);
 parse_error=1;assert(!corpus_runner_step(&r)&&closed==1);
 r=setup(false);corpus_runner_cancel(&r);assert(!r.running&&!r.candidate&&closed==1&&strstr(r.text,"Cancelled"));
 assert(!corpus_runner_step(&r));return 0;}
''')

    def test_gui_lifetime_and_registry_contracts(self):
        app = (ROOT / "applications/main/subghz/subghz.c").read_text()
        allocation = function(app, "SubGhz* subghz_alloc(")
        for field in ("workspace_plugin", "workspace_plugin_manager", "workspace_context"):
            self.assertIn(f"subghz->{field} = NULL;", allocation)
        ui = (CORPUS / "corpus_ui.c").read_text()
        self.assertNotIn("view_set_context(submenu_get_view", ui)
        self.assertIn("view_dispatcher_send_custom_event", ui)
        runner = (CORPUS / "corpus_runner.c").read_text()
        self.assertIn('EXT_PATH("apps_data/subghz/plugins")', runner)
        self.assertIn("loaded_plugin_count != report->expected_plugin_count", runner)
        shim = (ROOT / "applications/main/subghz/scenes/subghz_scene_workspaces.c").read_text()
        cleanup = function(shim, "void subghz_scene_workspaces_on_exit(")
        self.assertLess(cleanup.index("->free("), cleanup.index("subghz_feature_plugin_unload("))
        self.assertNotIn("scene_manager_next_scene", (PROFILE / "workspaces.c").read_text())


if __name__ == "__main__":
    unittest.main()
