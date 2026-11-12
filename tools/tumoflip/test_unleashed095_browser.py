"""Actual file-browser scans must terminate on SD failure, not leave a spinner."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT=Path(__file__).resolve().parents[2]
WORKER=ROOT/'applications/services/gui/modules/file_browser_worker.c'

PRELUDE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define FILE_NAME_LEN_MAX 256
#define LONG_LOAD_THRESHOLD 100
#define RECORD_STORAGE "storage"
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
#define furi_check assert
enum {FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL};
typedef int FS_Error;
typedef int Storage;
typedef struct {unsigned index;FS_Error error;} File;
typedef struct {bool dir;} FileInfo;
typedef struct {char data[256];} FuriString;
typedef struct {void* cb_ctx;void(*long_load_cb)(void*);
 void(*folder_cb)(void*,uint32_t,int32_t,bool);void(*folder_error_cb)(void*);
 void(*list_load_cb)(void*,uint32_t);void(*list_item_cb)(void*,FuriString*,uint32_t,bool,bool);} BrowserWorker;
static unsigned entries=3,fail_at=999,errors,lasts,rows,loads,opened;
static bool open_ok=true;
static Storage* furi_record_open(const char* n){(void)n;opened++;static Storage s;return &s;}
static void furi_record_close(const char* n){(void)n;assert(opened);opened--;}
static File* storage_file_alloc(Storage* s){(void)s;return calloc(1,sizeof(File));}
static void storage_file_free(File* f){free(f);}
static bool storage_dir_open(File* f,const char* p){(void)p;f->error=open_ok?FSE_OK:FSE_INTERNAL;return open_ok;}
static bool storage_dir_read(File* f,FileInfo* i,char* n,size_t count){
 if(f->index==fail_at){f->error=FSE_INTERNAL;return false;}
 if(f->index==entries){f->error=FSE_NOT_EXIST;return false;}
 f->error=FSE_OK;i->dir=false;snprintf(n,count,"file%u",f->index++);return true;
}
static void storage_dir_close(File* f){(void)f;}
static FS_Error storage_file_get_error(File* f){return f->error;}
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static void furi_string_free(FuriString* s){free(s);}
static const char* furi_string_get_cstr(FuriString* s){return s->data;}
static void furi_string_set(FuriString* s,const char* t){snprintf(s->data,sizeof(s->data),"%s",t);}
static void furi_string_printf(FuriString* s,const char* f,...){va_list a;va_start(a,f);vsnprintf(s->data,sizeof(s->data),f,a);va_end(a);}
static bool furi_string_empty(FuriString* s){return !s->data[0];}
static int furi_string_cmp(FuriString* a,FuriString* b){return strcmp(a->data,b->data);}
static bool file_info_is_dir(FileInfo* f){return f->dir;}
static bool browser_filter_by_name(BrowserWorker* b,FuriString* s,bool dir){(void)b;(void)s;(void)dir;return true;}
static void error_cb(void* c){(void)c;errors++;}
static void load_cb(void* c,uint32_t offset){(void)c;(void)offset;loads++;rows=0;}
static void item_cb(void* c,FuriString* p,uint32_t index,bool folder,bool last){
 (void)c;(void)p;(void)index;(void)folder;if(last)lasts++;else rows++;
}
'''


class BrowserFailureTests(unittest.TestCase):
    def run_scan(self, mode):
        source=WORKER.read_text()
        helpers=''
        for signature in ('static void browser_log_error(', 'static bool browser_dir_read_complete(',
                          'static void browser_load_aborted(', 'static void browser_load_failed('):
            if signature in source:helpers+=function(source,signature)+'\n'
        actual='\n'.join(function(source,s) for s in ('static bool browser_folder_init(',
                       'static bool browser_folder_load_chunked(', 'static bool browser_folder_load_full('))
        native(PRELUDE+helpers+actual+r'''
int main(void){
 BrowserWorker b={.folder_error_cb=error_cb,.list_load_cb=load_cb,.list_item_cb=item_cb};
 FuriString path={"/ext/test"},filename={""};uint32_t count=0;int32_t index=0;
 unsigned mode=MODE;
 if(mode==0){open_ok=false;assert(!browser_folder_load_full(&b,&path));assert(errors && lasts && loads);}
 if(mode==1){fail_at=1;assert(!browser_folder_init(&b,&path,&filename,&count,&index));}
 if(mode==2){fail_at=1;assert(!browser_folder_load_full(&b,&path));assert(errors && lasts);}
 if(mode==3){entries=1;assert(!browser_folder_load_chunked(&b,&path,3,2));assert(!errors && lasts && loads);}
 if(mode==4){fail_at=1;assert(!browser_folder_load_chunked(&b,&path,3,2));assert(errors && lasts);}
 if(mode==5){assert(browser_folder_init(&b,&path,&filename,&count,&index));assert(count==3 && index==-1);
  assert(browser_folder_load_full(&b,&path));assert(rows==3 && lasts==1 && !errors);
  assert(browser_folder_load_chunked(&b,&path,1,2));assert(rows==2 && !errors);}
 assert(!opened);return 0;
}
'''.replace('MODE',str(mode)))

    def test_scan_failures_terminate_callbacks_and_eof_is_not_an_error(self):
        for mode in range(6):
            with self.subTest(mode=mode):self.run_scan(mode)

    def test_native_browser_uses_deferred_start_without_changing_public_worker_contract(self):
        source=(ROOT/'applications/services/gui/modules/file_browser.c').read_text()
        start=function(source,'void file_browser_start(')
        self.assertIn('file_browser_worker_alloc_deferred(',start)
        self.assertLess(start.index('file_browser_worker_set_long_load_callback('),start.index('file_browser_worker_start('))
        callback=function(source,'static void browser_folder_error_cb(void* context) {')
        self.assertIn('browser_list_load_cb(browser, 0)',callback)
        self.assertIn('model->list_loading = false',callback)
        self.assertIn('model->folder_loading = false',callback)
        api=(ROOT/'targets/f7/api_symbols.csv').read_text()
        for name in ('file_browser_worker_start','file_browser_worker_alloc_deferred','file_browser_worker_set_folder_error_callback'):
            self.assertNotIn('Function,+,'+name+',',api)

    def test_error_callback_drops_stale_rows_and_allows_parent_navigation(self):
        actual=function((ROOT/'applications/services/gui/modules/file_browser.c').read_text(),
                        'static void browser_folder_error_cb(void* context) {')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define furi_check assert
#define with_view_model(v,decl,body,update) do {decl=(v);body;}while(0)
typedef struct {bool is_root,folder_error,folder_loading,list_loading;
 uint32_t item_cnt,list_offset;int32_t item_idx;} FileBrowserModel;
typedef struct {FileBrowserModel* view;} FileBrowser;
static unsigned resets;
static void browser_list_load_cb(void* context,uint32_t offset){(void)context;assert(!offset);resets++;}
''' + actual + r'''
int main(void){FileBrowserModel m={.item_cnt=99,.item_idx=70,.folder_loading=true,.list_loading=true};FileBrowser b={&m};
 browser_folder_error_cb(&b);assert(m.folder_error && !m.list_loading && !m.folder_loading && m.item_cnt==1 && m.item_idx==0 && resets==1);
 m.is_root=true;browser_folder_error_cb(&b);assert(!m.item_cnt && m.folder_error && resets==2);return 0;}
''')


if __name__=='__main__':unittest.main()
