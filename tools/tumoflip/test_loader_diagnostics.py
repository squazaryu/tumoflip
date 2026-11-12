"""Native checks for redacted, deterministic launch evidence and bounded metadata."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class LoaderDiagnosticsTest(unittest.TestCase):
    def test_codes_memory_evidence_and_redacted_output(self):
        header = (ROOT / "applications/services/loader/loader_diagnostics.h").read_text()
        source = (ROOT / "applications/services/loader/loader_diagnostics.c").read_text()
        production = "\n".join(l for l in (header + source).splitlines()
                               if not l.startswith(("#pragma", "#include")))
        native(r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define FAP_MANIFEST_MAGIC 123
#define FAP_MANIFEST_SUPPORTED_VERSION 1
typedef enum {FlipperApplicationPreloadStatusSuccess,FlipperApplicationPreloadStatusInvalidFile,
FlipperApplicationPreloadStatusNotEnoughMemory,FlipperApplicationPreloadStatusInvalidManifest,
FlipperApplicationPreloadStatusApiTooOld,FlipperApplicationPreloadStatusApiTooNew,
FlipperApplicationPreloadStatusTargetMismatch} FlipperApplicationPreloadStatus;
typedef struct{char data[512];} FuriString;
static void furi_string_printf(FuriString*s,const char*format,...){va_list ap;va_start(ap,format);int n=vsnprintf(s->data,sizeof(s->data),format,ap);va_end(ap);assert(n>=0&&(size_t)n<sizeof(s->data));}
static const struct {unsigned api_version_major,api_version_minor;} api={88,13};
#define firmware_api_interface (&api)
typedef struct {struct {unsigned manifest_magic,manifest_version;struct{unsigned major,minor;}api_version;unsigned hardware_target_id;}base;}FlipperApplicationManifest;
typedef struct {FlipperApplicationManifest manifest;size_t need,free,largest;}FlipperApplication;
static size_t memmgr_get_free_heap(void){return 40000;}
static size_t memmgr_heap_get_max_free_block(void){return 25000;}
static const FlipperApplicationManifest* flipper_application_get_manifest(FlipperApplication*a){return &a->manifest;}
static bool flipper_application_get_memory_failure(FlipperApplication*a,size_t*n,size_t*f,size_t*m){*n=a->need;*f=a->free;*m=a->largest;return a->need!=0;}
''' + production + r'''
int main(void){
 LoaderDiagnostic d={0};FuriString s;
 loader_diagnostic_begin(&d,"/ext/apps/Private\nFolder/App;code=ok.fap");
 assert(d.sequence==1&&!strchr(d.app,';')&&!strchr(d.app,'\n'));
 assert(!strstr(d.app,"Folder"));assert(d.free_heap==40000);
 FlipperApplication app={.need=8000,.free=20000,.largest=4000};
 loader_diagnostic_capture(&d,&app);assert(d.code==LoaderDiagnosticFragmented);
 assert(d.required==8000&&d.free_heap==20000&&d.max_block==4000);
 loader_diagnostic_format(&d,&s);assert(strstr(s.data,"code=fragmented_memory"));
 assert(strstr(s.data,"required=8000;free=20000;largest=4000"));
 app.free=5000;loader_diagnostic_capture(&d,&app);assert(d.code==LoaderDiagnosticMemory);
 loader_diagnostic_begin(&d,"NFC");assert(d.sequence==2&&d.required==0&&d.code==LoaderDiagnosticOk);
 loader_diagnostic_format(&d,&s);assert(!strstr(s.data,"fragmented"));
 assert(loader_diagnostic_preload_code(FlipperApplicationPreloadStatusInvalidManifest)==LoaderDiagnosticManifest);
 assert(loader_diagnostic_preload_code(FlipperApplicationPreloadStatusApiTooNew)==LoaderDiagnosticApiNew);
 assert(loader_diagnostic_preload_code(FlipperApplicationPreloadStatusTargetMismatch)==LoaderDiagnosticTarget);
 return 0;
}
''')

    def test_manifest_read_never_uses_untrusted_section_size(self):
        source = (ROOT / "lib/flipper_application/flipper_application.c").read_text()
        callback = function(source, "static bool flipper_application_process_manifest_section(")
        native(r'''
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int File;
typedef struct{unsigned char bytes[85];}FlipperApplicationManifest;
static size_t requested;static bool read_ok=true;
static bool storage_file_seek(File*f,size_t o,bool absolute){(void)f;(void)o;return absolute;}
static size_t storage_file_read(File*f,void*p,size_t n){(void)f;requested=n;memset(p,1,n);return read_ok?n:n-1;}
''' + callback + r'''
int main(void){
 struct {FlipperApplicationManifest m;unsigned guard;}dest={.guard=1234};
 assert(!flipper_application_process_manifest_section(NULL,0,84,&dest.m));
 assert(flipper_application_process_manifest_section(NULL,0,4096,&dest.m));
 assert(requested==85&&dest.guard==1234);
 read_ok=false;assert(!flipper_application_process_manifest_section(NULL,0,85,&dest.m));
 return 0;
}
''')

    def test_rpc_readout_and_atomic_launch_use_loader_queue(self):
        source = (ROOT / "applications/services/loader/loader.c").read_text()
        start = function(source, "LoaderStatus loader_start_with_diagnostics(")
        self.assertIn("LoaderMessageTypeStartWithDiagnostic", start)
        self.assertIn("api_lock_wait_unlock_and_free", start)
        self.assertIn("loader_diagnostic_format(&status.diagnostic, message.start.diagnostic)", source)
        runtime = (ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c").read_text()
        handler = function(runtime, "static void\n    tumoflip_runtime_handle_request(")
        self.assertIn('strcmp(command, "loader_diag")', handler)


if __name__ == "__main__":
    unittest.main()
