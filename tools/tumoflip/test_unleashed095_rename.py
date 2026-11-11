"""Case-only rename and copy failure checks; simulated SD, no real file mutation."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native
ROOT=Path(__file__).resolve().parents[2]
STORAGE=ROOT/'applications/services/storage/storage_external_api.c'


class RenameSafetyTests(unittest.TestCase):
    def test_failed_archive_rename_does_not_move_its_favorite(self):
        actual=function((ROOT/'applications/main/archive/helpers/archive_files.c').read_text(),
                        'FS_Error archive_rename_copy_file_or_dir(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#define furi_assert assert
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define RECORD_STORAGE "storage"
#define MAX_EXT_LEN 6
enum {FSE_OK,FSE_EXIST,FSE_INVALID_NAME,FSE_INTERNAL};typedef int FS_Error;typedef int Storage;
typedef struct {char data[256];} FuriString;
static unsigned favorites;static FS_Error outcome=FSE_INTERNAL;
static Storage* furi_record_open(const char* n){(void)n;static Storage s;return &s;}
static void furi_record_close(const char* n){(void)n;}
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static FuriString* furi_string_alloc_set(const char* p){FuriString* s=furi_string_alloc();snprintf(s->data,256,"%s",p);return s;}
static void furi_string_free(FuriString* s){free(s);}
static const char* furi_string_get_cstr(FuriString* s){return s->data;}
static void furi_string_cat_printf(FuriString* s,const char* f,...){(void)s;(void)f;}
static bool path_contains_only_ascii(const char* p){(void)p;return true;}
static void path_extract_filename(FuriString* p,FuriString* n,bool b){(void)p;(void)n;(void)b;}
static void path_extract_extension(FuriString* p,char* e,size_t n){(void)p;(void)e;(void)n;}
static void path_extract_dirname(const char* p,FuriString* n){(void)p;(void)n;}
static bool storage_common_equivalent_path(Storage* s,const char* a,const char* b){(void)s;return !strcasecmp(a,b);}
static bool storage_common_exists(Storage* s,const char* p){(void)s;(void)p;return false;}
static FS_Error storage_common_copy(Storage* s,const char* a,const char* b){(void)s;(void)a;(void)b;return outcome;}
static FS_Error storage_common_rename(Storage* s,const char* a,const char* b){return storage_common_copy(s,a,b);}
static void storage_get_next_filename(Storage* s,const char* d,const char* n,const char* e,FuriString* out,unsigned max){(void)s;(void)d;(void)n;(void)e;(void)out;(void)max;}
static bool archive_is_favorite(const char* f,...){(void)f;return true;}
static bool archive_favorites_rename(const char* a,const char* b){(void)a;(void)b;favorites++;return true;}
''' + actual + r'''
int main(void){int context=0;
 assert(archive_rename_copy_file_or_dir(&context,"/ext/old.sub","/ext/new.sub",false)!=FSE_OK && !favorites);
 outcome=FSE_OK;assert(archive_rename_copy_file_or_dir(&context,"/ext/old.sub","/ext/new.sub",false)==FSE_OK && favorites==1);
 assert(archive_rename_copy_file_or_dir(&context,"/ext/old.sub","/ext/copy.sub",true)==FSE_OK && favorites==1);return 0;}
''')

    def test_case_rename_keeps_payload_on_copy_failure_and_temp_collisions(self):
        source=STORAGE.read_text();helpers=''
        for signature in ('static bool storage_common_is_case_only_rename(',
                          'static FS_Error\n    storage_common_rename_case_only('):
            if signature in source:helpers+=function(source,signature)+'\n'
        actual=function(source,'FS_Error storage_common_rename(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
enum {FSE_OK,FSE_NOT_EXIST,FSE_INTERNAL,FSE_INVALID_NAME,FSE_EXIST};
typedef int FS_Error;typedef int Storage;
typedef struct {char text[256];} FuriString;
typedef struct {char path[256];unsigned payload;bool used;} Entry;
static Entry entries[24];static unsigned copies,fail_copy;
static int find(const char* p){for(unsigned i=0;i<24;i++)if(entries[i].used&&!strcasecmp(p,entries[i].path))return i;return -1;}
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static void furi_string_free(FuriString* s){free(s);}
static const char* furi_string_get_cstr(FuriString* s){return s->text;}
static void furi_string_printf(FuriString* s,const char* f,...){va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);}
static bool storage_common_exists(Storage* s,const char* p){(void)s;return find(p)>=0;}
static bool storage_dir_exists(Storage* s,const char* p){(void)s;(void)p;return false;}
static bool storage_file_exists(Storage* s,const char* p){return storage_common_exists(s,p);}
static bool storage_common_equivalent_path(Storage* s,const char* a,const char* b){(void)s;return !strcasecmp(a,b);}
static bool storage_common_is_subdir(Storage* s,const char* a,const char* b){(void)s;(void)a;(void)b;return false;}
static FS_Error storage_common_stat(Storage* s,const char* p,void* info){(void)s;(void)info;return find(p)<0?FSE_NOT_EXIST:FSE_OK;}
static FS_Error storage_common_remove(Storage* s,const char* p){(void)s;int i=find(p);if(i<0)return FSE_NOT_EXIST;entries[i].used=false;return FSE_OK;}
static bool storage_simply_remove_recursive(Storage* s,const char* p){return storage_common_remove(s,p)==FSE_OK;}
static FS_Error storage_common_copy(Storage* s,const char* a,const char* b){
 (void)s;copies++;if(copies==fail_copy)return FSE_INTERNAL;
 int i=find(a);if(i<0)return FSE_NOT_EXIST;if(find(b)>=0)return FSE_EXIST;
 for(unsigned j=0;j<24;j++)if(!entries[j].used){entries[j]=(Entry){.used=true,.payload=entries[i].payload};
  snprintf(entries[j].path,sizeof(entries[j].path),"%s",b);return FSE_OK;}
 return FSE_INTERNAL;
}
FS_Error storage_common_rename(Storage*,const char*,const char*);
''' + helpers + actual + r'''
static void reset(void){memset(entries,0,sizeof(entries));entries[0]=(Entry){.path="/ext/subghz/Own.sub",.payload=123,.used=true};copies=fail_copy=0;}
int main(void){Storage s=0;reset();
 (void)furi_string_alloc;(void)furi_string_free;(void)furi_string_get_cstr;
 (void)furi_string_printf;(void)storage_common_stat;
 assert(storage_common_rename(&s,"/ext/subghz/Own.sub","/ext/subghz/own.sub")==FSE_OK);
 assert(find("/ext/subghz/own.sub")>=0 && !strcmp(entries[find("/ext/subghz/own.sub")].path,"/ext/subghz/own.sub"));
 assert(entries[find("/ext/subghz/own.sub")].payload==123);
 for(unsigned fail=1;fail<=2;fail++){
  reset();fail_copy=fail;assert(storage_common_rename(&s,"/ext/subghz/Own.sub","/ext/subghz/own.sub")!=FSE_OK);
  assert(find("/ext/subghz/Own.sub")>=0 && entries[find("/ext/subghz/Own.sub")].payload==123);
 }
 reset();for(unsigned i=0;i<16;i++){entries[i+1]=(Entry){.used=true,.payload=900+i};snprintf(entries[i+1].path,256,"/ext/subghz/Own.sub.rnm%u",i);}
 assert(storage_common_rename(&s,"/ext/subghz/Own.sub","/ext/subghz/own.sub")!=FSE_OK && !copies);
 assert(entries[0].used&&entries[0].payload==123);
 reset();assert(storage_common_rename(&s,"/ext/subghz/Own.sub","/ext/subghz/Own.sub")==FSE_OK && !copies);
 reset();assert(storage_common_rename(&s,"/ext/subghz/Own.sub","/ext/other.sub")==FSE_OK);
 assert(entries[find("/ext/other.sub")].payload==123);return 0;
}
''')

    def test_short_or_unsynced_copy_is_not_reported_as_success(self):
        actual=function(STORAGE.read_text(),'FS_Error storage_common_copy(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define furi_check assert
enum {FSE_OK,FSE_INTERNAL};typedef int FS_Error;typedef int Storage;
enum {FSAM_READ,FSAM_WRITE,FSOM_OPEN_EXISTING,FSOM_CREATE_NEW};
typedef struct {unsigned size;bool dir;} FileInfo;typedef int Stream;
static size_t copied=3;static bool synced=true;
static FS_Error storage_common_stat(Storage* s,const char* p,FileInfo* f){(void)s;(void)p;f->size=3;f->dir=false;return FSE_OK;}
static bool file_info_is_dir(FileInfo* f){return f->dir;}
static FS_Error storage_copy_recursive(Storage* s,const char* a,const char* b){(void)s;(void)a;(void)b;return FSE_INTERNAL;}
static Stream* file_stream_alloc(Storage* s){(void)s;return malloc(sizeof(Stream));}
static bool file_stream_open(Stream* s,const char* p,int a,int m){(void)s;(void)p;(void)a;(void)m;return true;}
static size_t stream_copy_full(Stream* a,Stream* b){(void)a;(void)b;return copied;}
static bool file_stream_close(Stream* s){(void)s;return synced;}
static FS_Error file_stream_get_error(Stream* s){(void)s;return FSE_OK;}
static void stream_free(Stream* s){free(s);}
''' + actual + r'''
int main(void){(void)file_stream_close;Storage s=0;
 copied=2;assert(storage_common_copy(&s,"a","b")!=FSE_OK);
 copied=3;synced=false;assert(storage_common_copy(&s,"a","b")!=FSE_OK);
 synced=true;assert(storage_common_copy(&s,"a","b")==FSE_OK);return 0;}
''')

    def test_validator_allows_case_change_and_initializes_optional_name(self):
        source=(ROOT/'applications/services/gui/modules/validators.c').read_text()
        struct=re.search(r'struct ValidatorIsFile \{.*?\n\};',source,re.S).group()
        actual='\n'.join(function(source,s) for s in ('bool validator_is_file_callback(',
             'ValidatorIsFile* validator_is_file_alloc_init(', 'void validator_is_file_free('))
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define furi_check assert
#define RECORD_STORAGE "storage"
enum {FSE_OK,FSE_NOT_EXIST};typedef int Storage;
typedef struct {char data[256];} FuriString;typedef struct ValidatorIsFile ValidatorIsFile;
static void* poison_alloc(size_t n){void* p=malloc(n);memset(p,0xA5,n);return p;}
static char* own_strdup(const char* s){char* p=calloc(strlen(s)+1,1);strcpy(p,s);return p;}
#define malloc poison_alloc
#define strdup own_strdup
static unsigned stats;
static FuriString* furi_string_alloc_printf(const char* f,...){FuriString* s=calloc(1,sizeof(*s));va_list a;va_start(a,f);vsnprintf(s->data,256,f,a);va_end(a);return s;}
static const char* furi_string_get_cstr(FuriString* s){return s->data;}
static void furi_string_printf(FuriString* s,const char* f,...){(void)s;(void)f;}
static void furi_string_free(FuriString* s){free(s);}
static Storage* furi_record_open(const char* n){(void)n;static Storage s;return &s;}
static void furi_record_close(const char* n){(void)n;}
static int storage_common_stat(Storage* s,const char* p,void* f){(void)s;(void)f;stats++;return strstr(p,"Other")?FSE_NOT_EXIST:FSE_OK;}
''' + struct + actual + r'''
int main(void){FuriString err={0};ValidatorIsFile* v=validator_is_file_alloc_init("/ext",".sub","Own");
 assert(validator_is_file_callback("own",&err,v) && !stats);
 assert(validator_is_file_callback("Other",&err,v));assert(!validator_is_file_callback("Taken",&err,v));validator_is_file_free(v);
 v=validator_is_file_alloc_init("/ext",".sub",NULL);assert(v->current_name==NULL);validator_is_file_free(v);return 0;}
''')
