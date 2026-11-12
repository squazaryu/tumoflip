"""Execute real capture I/O and export code against bounded storage fixtures."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c, function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/capture_inspector"


def source(name):
    return "\n".join(line for line in (APP / name).read_text().splitlines()
                     if not line.startswith(("#include", "#pragma")))


class CaptureStorageTests(unittest.TestCase):
    def test_read_only_load_errors_cancellation_and_path_bounds(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int Storage;
typedef struct {size_t pos;bool open;} File;
enum {FSAM_READ,FSOM_OPEN_EXISTING};
static char data[4096],original[4096];static size_t data_size;
static int mode,allocs,reads;
static File* storage_file_alloc(Storage* s){(void)s;allocs++;return calloc(1,sizeof(File));}
static void storage_file_free(File* f){assert(!f->open);allocs--;free(f);}
static bool storage_file_open(File* f,const char* path,int access,int how){
 assert(!strncmp(path,"/ext/",5));assert(access==FSAM_READ&&how==FSOM_OPEN_EXISTING);
 f->open=mode!=2;return f->open;
}
static uint64_t storage_file_size(File* f){(void)f;return mode==6&&reads?data_size+1:data_size;}
static bool storage_file_close(File* f){f->open=false;return mode!=3;}
static size_t storage_file_read(File* f,void* out,size_t n){
 assert(f->open);reads++;if(mode==1)n--;
 memcpy(out,data+f->pos,n);f->pos+=n;return n;
}
static bool cancel(void* ctx){(void)ctx;return mode==4 || (mode==5&&reads>0);}
''' + source("capture_model.h") + source("capture_storage.h")
        + source("capture_model.c") + source("capture_storage.c") + r'''
int main(void){
 assert(ci_capture_path_valid("/ext/subghz/test.PSF"));
 const char* invalid[]={NULL,"/int/a.sub","/ext/a.nfc","/ext/../x.sub","/ext/./a.sub","/ext/x\ny.sub"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!ci_capture_path_valid(invalid[i]));
 strcpy(data,"Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
             "Preset: AM650\nProtocol: Test\nKey: 00 00\n");
 for(int i=0;i<50;i++)strcat(data,"# unchanged\n");
 data_size=strlen(data);memcpy(original,data,sizeof(data));
 CiSnapshot* s=malloc(sizeof(*s));assert(s);
 for(mode=0;mode<=6;mode++){
  reads=allocs=0;CiLoadStatus status=ci_capture_load(NULL,"/ext/a.sub",s,cancel,NULL);
  if(!mode)assert(status==CiLoadOk && s->finished);
  else {assert(status==(mode==4||mode==5?CiLoadCancelled:CiLoadIoError));assert(!s->finished);}
  assert(!allocs&&!memcmp(original,data,sizeof(data)));
 }
 mode=0;assert(ci_capture_load(NULL,"/int/a.sub",s,NULL,NULL)==CiLoadBadPath);
 data_size=CI_FILE_CAP+1;assert(ci_capture_load(NULL,"/ext/a.sub",s,NULL,NULL)==CiLoadParseError);
 data_size=0;assert(ci_capture_load(NULL,"/ext/a.sub",s,NULL,NULL)==CiLoadParseError);
 strcpy(data,"bad\n");data_size=4;assert(ci_capture_load(NULL,"/ext/a.sub",s,NULL,NULL)==CiLoadParseError);
 assert(!s->finished&&!allocs);free(s);return 0;
}
''')

    def test_export_keeps_cleanup_failure_visible_after_cancel(self):
        app_source = (APP / "capture_inspector.c").read_text()
        body = function(app_source, "static bool ci_write_string(")
        body += function(app_source, "static bool ci_series_paths_are_distinct(")
        body += function(app_source, "static void ci_export_report(")
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define CI_REPORT_DIR "/ext/apps_data/capture_inspector"
typedef struct {char value[1024];} FuriString;
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static void furi_string_free(FuriString* s){free(s);}
static const char* furi_string_get_cstr(FuriString* s){return s->value;}
static void furi_string_set(FuriString* s,const char* v){snprintf(s->value,sizeof(s->value),"%s",v);}
static void furi_string_printf(FuriString* s,const char* f,...){va_list ap;va_start(ap,f);vsnprintf(s->value,sizeof(s->value),f,ap);va_end(ap);}
static void furi_string_cat(FuriString* s,const char* v){size_t n=strlen(s->value);snprintf(s->value+n,sizeof(s->value)-n,"%s",v);}
static void furi_string_cat_printf(FuriString* s,const char* f,...){size_t n=strlen(s->value);va_list ap;va_start(ap,f);vsnprintf(s->value+n,sizeof(s->value)-n,f,ap);va_end(ap);}
typedef int Storage;
typedef struct {bool open;} File;
typedef int FS_Error;
enum {FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL,FSAM_WRITE,FSOM_CREATE_NEW};
static int mode,writes,removes,allocs;static bool exists;
static File* storage_file_alloc(Storage* s){(void)s;allocs++;return calloc(1,sizeof(File));}
static void storage_file_free(File* f){assert(!f->open);allocs--;free(f);}
static bool storage_simply_mkdir(Storage* s,const char* p){(void)s;assert(!strcmp(p,CI_REPORT_DIR));return true;}
static FS_Error storage_common_stat(Storage* s,const char* p,void* out){
 (void)s;(void)out;assert(!strncmp(p,CI_REPORT_DIR,strlen(CI_REPORT_DIR)));return FSE_NOT_EXIST;
}
static bool storage_file_open(File* f,const char* p,int access,int how){
 assert(strstr(p,"compare_"));assert(access==FSAM_WRITE&&how==FSOM_CREATE_NEW);
 if(mode==1)return false;
 exists=f->open=true;
 return true;
}
static size_t storage_file_write(File* f,const void* d,size_t n){assert(f->open);(void)d;writes++;return mode==2?n-1:n;}
static bool storage_file_sync(File* f){assert(f->open);return mode!=3;}
static bool storage_file_close(File* f){f->open=false;return mode!=4;}
static FS_Error storage_common_remove(Storage* s,const char* p){(void)s;assert(strstr(p,"compare_"));removes++;if(mode==6)return FSE_INTERNAL;exists=false;return FSE_OK;}
static bool ci_cancelled(void* c){(void)c;return (mode==5||mode==6)&&writes>=2;}
''' + source("capture_model.h") + source("capture_model.c") + r'''
typedef struct {Storage* storage;FuriString* result;FuriString* paths[CI_SERIES_MAX];CiSnapshot captures[CI_SERIES_MAX];bool ready[CI_SERIES_MAX];} CiApp;
''' + body + r'''
int main(void){
 CiApp* a=calloc(1,sizeof(*a));a->result=furi_string_alloc();a->paths[0]=furi_string_alloc();a->paths[1]=furi_string_alloc();a->paths[2]=furi_string_alloc();
 furi_string_set(a->paths[0],"/ext/a.sub");furi_string_set(a->paths[1],"/ext/b.sub");
 for(int i=0;i<2;i++){a->captures[i].finished=true;a->captures[i].count=1;strcpy(a->captures[i].fields[0].key,"Key");strcpy(a->captures[i].fields[0].value,"stored");}
 for(mode=0;mode<=6;mode++){
  allocs=writes=removes=0;exists=false;ci_export_report(a);assert(!allocs);
  if(mode==0)assert(exists&&strstr(a->result->value,"saved"));
  else if(mode==6)assert(exists&&strstr(a->result->value,"remain"));
  else assert(!exists);
  for(int i=0;i<2;i++)assert(!strcmp(a->captures[i].fields[0].value,"stored"));
 }
 furi_string_free(a->result);furi_string_free(a->paths[0]);furi_string_free(a->paths[1]);furi_string_free(a->paths[2]);free(a);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
